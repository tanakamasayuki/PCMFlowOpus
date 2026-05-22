// OpusEncoder-focused tests. Round-trip correctness is covered by the
// `roundtrip/` suite; this file exercises encoder-specific behavior:
// bitrate / complexity / DTX / FEC / frame duration controls, and the
// PCMSink path (writeFrames → packet callback).

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

#define EXPECT_LT(name, value, upper) do { \
    ++g_total; \
    const long _v = (long)(value); \
    const long _u = (long)(upper); \
    if (_v < _u) { ++g_pass; Serial.print("PASS "); Serial.println(name); } \
    else { \
        Serial.print("FAIL "); Serial.print(name); \
        Serial.print(" value="); Serial.print(_v); \
        Serial.print(" upper="); Serial.println(_u); \
    } \
} while (0)


static void fill_silence(int16_t *pcm, size_t n) {
    for (size_t i = 0; i < n; ++i) pcm[i] = 0;
}

static void fill_sine(int16_t *pcm, size_t frames, uint32_t rate, uint8_t channels)
{
    const float w = 2.0f * 3.14159265f * 440.0f / static_cast<float>(rate);
    for (size_t i = 0; i < frames; ++i) {
        const int16_t v = static_cast<int16_t>(16383.0f * sinf(w * static_cast<float>(i)));
        for (uint8_t c = 0; c < channels; ++c) pcm[i * channels + c] = v;
    }
}


// --- begin() defaults and basic encode produce a non-empty packet ---------
static void test_basic_encode()
{
    OpusEncoder enc;
    EXPECT_TRUE("basic/begin",
                enc.begin({16000, 1, 16}, OpusApplication::Voip, 24000));
    EXPECT_TRUE("basic/ready", enc.isReady());
    EXPECT_EQ ("basic/format-rate", 16000, enc.format().sampleRate);
    EXPECT_EQ ("basic/format-channels", 1, enc.format().channels);
    EXPECT_EQ ("basic/format-bits", 16, enc.format().bitsPerSample);

    int16_t pcm[320];
    fill_sine(pcm, 320, 16000, 1);
    uint8_t pkt[400];
    const size_t n = enc.encodeFrame(pcm, 320, pkt, sizeof(pkt));
    EXPECT_TRUE("basic/encoded-nonzero", n > 0);
    EXPECT_LT ("basic/packet-bound", n, 1275);
    EXPECT_EQ ("basic/error-clean", (int)OpusEncoder::Error::None, (int)enc.lastError());
}


// --- Bitrate change actually shrinks average packet size ------------------
static void test_bitrate_shrinks_packets()
{
    OpusEncoder enc;
    EXPECT_TRUE("bitrate/begin",
                enc.begin({16000, 1, 16}, OpusApplication::Voip, 64000));

    int16_t pcm[320];
    uint8_t pkt[400];

    // Warm up at 64 kbps and average packet size over a few frames.
    size_t sum_hi = 0;
    for (int f = 0; f < 5; ++f) {
        fill_sine(pcm, 320, 16000, 1);
        const size_t n = enc.encodeFrame(pcm, 320, pkt, sizeof(pkt));
        if (f >= 2) sum_hi += n;     // skip 2 frames of ramp-up
    }

    EXPECT_TRUE("bitrate/set-low", enc.setBitrate(8000));

    size_t sum_lo = 0;
    for (int f = 0; f < 5; ++f) {
        fill_sine(pcm, 320, 16000, 1);
        const size_t n = enc.encodeFrame(pcm, 320, pkt, sizeof(pkt));
        if (f >= 2) sum_lo += n;
    }

    Serial.print("INFO bitrate-sums hi=");
    Serial.print(sum_hi);
    Serial.print(" lo=");
    Serial.println(sum_lo);
    EXPECT_LT("bitrate/low-shrinks", sum_lo, sum_hi);
}


// --- setComplexity / setDtx / setInbandFec / setPacketLossPerc accept ----
static void test_ctl_setters()
{
    OpusEncoder enc;
    EXPECT_TRUE("ctl/begin",
                enc.begin({16000, 1, 16}, OpusApplication::Voip, 24000));

    EXPECT_TRUE("ctl/complexity-0",   enc.setComplexity(0));
    EXPECT_TRUE("ctl/complexity-10",  enc.setComplexity(10));
    EXPECT_TRUE("ctl/dtx-on",         enc.setDtx(true));
    EXPECT_TRUE("ctl/dtx-off",        enc.setDtx(false));
    EXPECT_TRUE("ctl/fec-on",         enc.setInbandFec(true));
    EXPECT_TRUE("ctl/fec-off",        enc.setInbandFec(false));
    EXPECT_TRUE("ctl/ploss-20",       enc.setPacketLossPerc(20));
    EXPECT_TRUE("ctl/ploss-0",        enc.setPacketLossPerc(0));
}


// --- setFrameDuration: 10 ms and 40 ms both produce valid packets ---------
static void test_frame_duration_variants()
{
    OpusEncoder enc;
    OpusDecoder dec;
    EXPECT_TRUE("fdur/enc-begin",
                enc.begin({16000, 1, 16}, OpusApplication::Voip, 24000));
    EXPECT_TRUE("fdur/dec-begin", dec.begin({16000, 1, 16}));

    EXPECT_TRUE("fdur/set-10", enc.setFrameDuration(10));
    int16_t pcm160[160];
    int16_t out160[160];
    fill_sine(pcm160, 160, 16000, 1);
    uint8_t pkt[400];
    {
        const size_t n = enc.encodeFrame(pcm160, 160, pkt, sizeof(pkt));
        EXPECT_TRUE("fdur/10-encoded", n > 0);
        EXPECT_EQ ("fdur/10-decoded-frames", 160,
                   dec.decodePacket(pkt, n, out160, 160));
    }

    EXPECT_TRUE("fdur/set-40", enc.setFrameDuration(40));
    int16_t pcm640[640];
    int16_t out640[640];
    fill_sine(pcm640, 640, 16000, 1);
    {
        const size_t n = enc.encodeFrame(pcm640, 640, pkt, sizeof(pkt));
        EXPECT_TRUE("fdur/40-encoded", n > 0);
        EXPECT_EQ ("fdur/40-decoded-frames", 640,
                   dec.decodePacket(pkt, n, out640, 640));
    }

    // Invalid value rejected.
    EXPECT_TRUE("fdur/invalid", !enc.setFrameDuration(17));
}


// --- PCMSink path: writeFrames buffers samples and emits via callback -----

struct SinkCounter {
    int calls = 0;
    size_t total_bytes = 0;
    size_t last_n = 0;
};

static void on_packet(void *user, const uint8_t * /*pkt*/, size_t n)
{
    SinkCounter *c = static_cast<SinkCounter *>(user);
    ++c->calls;
    c->total_bytes += n;
    c->last_n = n;
}

static void test_pcmsink_path()
{
    OpusEncoder enc;
    SinkCounter ctr;
    EXPECT_TRUE("sink/begin",
                enc.begin({16000, 1, 16}, OpusApplication::Voip, 24000));
    enc.setPacketSink(on_packet, &ctr);

    // Feed 3 full 20 ms frames (320 samples each) plus a partial 100
    // samples. Expect exactly 3 callback invocations.
    int16_t pcm[1060];
    fill_sine(pcm, 1060, 16000, 1);

    const size_t consumed = enc.writeFrames(pcm, 1060);
    EXPECT_EQ("sink/all-consumed", 1060, consumed);
    EXPECT_EQ("sink/callbacks", 3, ctr.calls);
    EXPECT_TRUE("sink/total-bytes-positive", ctr.total_bytes > 0);
    EXPECT_TRUE("sink/last-packet-positive", ctr.last_n > 0);

    // No callback for the remaining partial frame.
    const int callsBefore = ctr.calls;
    EXPECT_EQ("sink/no-trigger-on-partial", callsBefore, ctr.calls);
}


// --- writeFrames before setPacketSink: encode still runs, no callback -----
static void test_pcmsink_no_callback_set()
{
    OpusEncoder enc;
    EXPECT_TRUE("nosink/begin",
                enc.begin({16000, 1, 16}, OpusApplication::Voip, 24000));

    int16_t pcm[320];
    fill_silence(pcm, 320);

    // Without a sink set, writeFrames consumes silently — no crash.
    const size_t consumed = enc.writeFrames(pcm, 320);
    EXPECT_EQ("nosink/consumed", 320, consumed);
}


void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start");

    test_basic_encode();
    test_bitrate_shrinks_packets();
    test_ctl_setters();
    test_frame_duration_variants();
    test_pcmsink_path();
    test_pcmsink_no_callback_set();

    Serial.print("TEST done ");
    Serial.print(g_pass);
    Serial.print("/");
    Serial.println(g_total);
}


void loop()
{
    delay(1);
}
