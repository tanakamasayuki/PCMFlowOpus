// OpusDecoder-focused tests. Round-trip correctness is covered by the
// `roundtrip/` suite; this file exercises decoder-specific behavior:
// PLC across consecutive losses, FEC recovery, the PCMSource path
// (decodePacket(nullptr-output) followed by readFrames), and stereo.
//
// Ground-truth packets are produced by our own OpusEncoder rather than
// shipped fixtures. ffmpeg-based golden vectors will be added once
// tools/gen_test_audio.py is implemented.

#include <PCMFlowOpus.h>
#include <math.h>

static int g_pass  = 0;
static int g_total = 0;

#define EXPECT_TRUE(name, cond) do { \
    ++g_total; \
    if (cond) { ++g_pass; Serial.print("PASS "); Serial.println(name); } \
    else { Serial.print("FAIL "); Serial.print(name); Serial.println(" cond"); } \
} while (0)

#define EXPECT_EQ(name, expected, actual) do { \
    ++g_total; \
    const long _e = (long)(expected); \
    const long _a = (long)(actual); \
    if (_e == _a) { ++g_pass; Serial.print("PASS "); Serial.println(name); } \
    else { \
        Serial.print("FAIL "); Serial.print(name); \
        Serial.print(" expected="); Serial.print(_e); \
        Serial.print(" actual=");   Serial.println(_a); \
    } \
} while (0)


static void fill_sine(int16_t *pcm, size_t frames, uint32_t rate, uint8_t channels,
                      size_t phase)
{
    const float w = 2.0f * 3.14159265f * 440.0f / static_cast<float>(rate);
    for (size_t i = 0; i < frames; ++i) {
        const int16_t v = static_cast<int16_t>(16383.0f * sinf(w * static_cast<float>(i + phase)));
        for (uint8_t c = 0; c < channels; ++c) pcm[i * channels + c] = v;
    }
}


// --- Basic begin / format reporting --------------------------------------
static void test_basic()
{
    OpusDecoder dec;
    EXPECT_TRUE("basic/begin",     dec.begin({16000, 1, 16}));
    EXPECT_TRUE("basic/ready",     dec.isReady());
    EXPECT_EQ ("basic/rate",       16000, dec.format().sampleRate);
    EXPECT_EQ ("basic/channels",   1,     dec.format().channels);
    EXPECT_EQ ("basic/bits",       16,    dec.format().bitsPerSample);
    EXPECT_EQ ("basic/eof-false",  0,     dec.isEof() ? 1 : 0);
    EXPECT_EQ ("basic/error",      (int)OpusDecoder::Error::None, (int)dec.lastError());
}


// --- PLC: 3 consecutive lost packets each yield expected frame count -----
static void test_plc_consecutive_losses()
{
    OpusEncoder enc;
    OpusDecoder dec;
    EXPECT_TRUE("plc/enc-begin",
                enc.begin({16000, 1, 16}, OpusApplication::Voip, 24000));
    EXPECT_TRUE("plc/dec-begin", dec.begin({16000, 1, 16}));

    // Establish history with a few good frames first.
    int16_t pcm[320];
    uint8_t pkt[400];
    int16_t out[320];
    for (int f = 0; f < 3; ++f) {
        fill_sine(pcm, 320, 16000, 1, f * 320);
        const size_t n = enc.encodeFrame(pcm, 320, pkt, sizeof(pkt));
        dec.decodePacket(pkt, n, out, 320);
    }

    // Three consecutive losses — each should still synthesize 20 ms of audio.
    for (int i = 0; i < 3; ++i) {
        const size_t got = dec.decodePacket(nullptr, 0, out, 320);
        char name[20]; sprintf(name, "plc/loss-%d-frames", i);
        EXPECT_EQ(name, 320, got);
    }

    // Recovery: next real packet decodes cleanly.
    fill_sine(pcm, 320, 16000, 1, 6 * 320);
    const size_t n_recover = enc.encodeFrame(pcm, 320, pkt, sizeof(pkt));
    EXPECT_EQ("plc/recover", 320, dec.decodePacket(pkt, n_recover, out, 320));
}


// --- FEC: in-band redundancy recovers the previous packet's audio --------
static void test_fec_recovery()
{
    OpusEncoder enc;
    OpusDecoder dec;
    EXPECT_TRUE("fec/enc-begin",
                enc.begin({16000, 1, 16}, OpusApplication::Voip, 24000));
    EXPECT_TRUE("fec/dec-begin", dec.begin({16000, 1, 16}));

    // Encoder side: enable FEC + tell it to assume 30 % loss so it
    // actually invests bits in redundancy.
    EXPECT_TRUE("fec/enable",  enc.setInbandFec(true));
    EXPECT_TRUE("fec/ploss",   enc.setPacketLossPerc(30));

    // Warm-up so encoder settles.
    int16_t pcm[320];
    uint8_t pkt_a[400];
    uint8_t pkt_b[400];
    int16_t out[320];
    for (int f = 0; f < 3; ++f) {
        fill_sine(pcm, 320, 16000, 1, f * 320);
        const size_t n = enc.encodeFrame(pcm, 320, pkt_a, sizeof(pkt_a));
        dec.decodePacket(pkt_a, n, out, 320);
    }

    // Frame A: encoded and held; pretend it was lost in transit.
    fill_sine(pcm, 320, 16000, 1, 3 * 320);
    const size_t n_a = enc.encodeFrame(pcm, 320, pkt_a, sizeof(pkt_a));
    EXPECT_TRUE("fec/A-encoded", n_a > 0);

    // Frame B: encoded right after; this packet carries A's FEC payload.
    fill_sine(pcm, 320, 16000, 1, 4 * 320);
    const size_t n_b = enc.encodeFrame(pcm, 320, pkt_b, sizeof(pkt_b));
    EXPECT_TRUE("fec/B-encoded", n_b > 0);

    // Recover A from B's redundancy.
    const size_t got_a = dec.decodePacketFec(pkt_b, n_b, out, 320);
    EXPECT_EQ("fec/recovered-A-frames", 320, got_a);

    // Then decode B normally.
    const size_t got_b = dec.decodePacket(pkt_b, n_b, out, 320);
    EXPECT_EQ("fec/decoded-B-frames", 320, got_b);
}


// --- PCMSource path: decodePacket(nullptr out) + readFrames() ------------
static void test_pcmsource_path()
{
    OpusEncoder enc;
    OpusDecoder dec;
    EXPECT_TRUE("src/enc-begin",
                enc.begin({16000, 1, 16}, OpusApplication::Voip, 24000));
    EXPECT_TRUE("src/dec-begin", dec.begin({16000, 1, 16}));

    int16_t pcm[320];
    uint8_t pkt[400];
    fill_sine(pcm, 320, 16000, 1, 0);
    const size_t n = enc.encodeFrame(pcm, 320, pkt, sizeof(pkt));

    // Decode into the internal buffer (pcm=nullptr, maxFrames=0). The
    // function returns how many frames it queued internally.
    const size_t queued = dec.decodePacket(pkt, n, nullptr, 0);
    EXPECT_EQ("src/queued", 320, queued);

    // Drain via the PCMSource interface in two pieces.
    int16_t out[200];
    const size_t got1 = dec.readFrames(out, 200);
    EXPECT_EQ("src/first-drain", 200, got1);
    const size_t got2 = dec.readFrames(out, 200);
    EXPECT_EQ("src/second-drain", 120, got2);   // 320 - 200
    const size_t got3 = dec.readFrames(out, 200);
    EXPECT_EQ("src/empty-drain", 0, got3);
}


// --- Stereo path -----------------------------------------------------------
static void test_stereo()
{
    OpusEncoder enc;
    OpusDecoder dec;
    EXPECT_TRUE("stereo/enc-begin",
                enc.begin({48000, 2, 16}, OpusApplication::Audio, 64000));
    EXPECT_TRUE("stereo/dec-begin", dec.begin({48000, 2, 16}));

    static int16_t pcm[960 * 2];
    uint8_t pkt[800];
    static int16_t out[960 * 2];

    for (int f = 0; f < 3; ++f) {
        fill_sine(pcm, 960, 48000, 2, f * 960);
        const size_t n = enc.encodeFrame(pcm, 960, pkt, sizeof(pkt));
        EXPECT_TRUE("stereo/encoded", n > 0);
        const size_t got = dec.decodePacket(pkt, n, out, 960);
        EXPECT_EQ("stereo/decoded-frames", 960, got);
    }
}


// --- Invalid inputs --------------------------------------------------------
static void test_invalid()
{
    OpusDecoder dec;
    EXPECT_TRUE("err/bad-rate", !dec.begin({22050, 1, 16}));
    EXPECT_EQ ("err/bad-rate-code",
               (int)OpusDecoder::Error::UnsupportedRate, (int)dec.lastError());
    EXPECT_TRUE("err/bad-channels", !dec.begin({16000, 5, 16}));
    EXPECT_TRUE("err/bad-bits",     !dec.begin({16000, 1, 24}));

    // decodePacket before begin() returns 0 and sets NotReady.
    int16_t out[160];
    uint8_t dummy[4] = {0, 0, 0, 0};
    EXPECT_EQ("err/decode-not-ready", 0,
              dec.decodePacket(dummy, sizeof(dummy), out, 160));
}


void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start");

    test_basic();
    test_plc_consecutive_losses();
    test_fec_recovery();
    test_pcmsource_path();
    test_stereo();
    test_invalid();

    Serial.print("TEST done ");
    Serial.print(g_pass);
    Serial.print("/");
    Serial.println(g_total);
}


void loop()
{
    delay(1);
}
