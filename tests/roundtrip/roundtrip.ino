// Encode → decode roundtrip tests.
//
// Self-contained: generates a 440 Hz sine wave in-sketch, encodes it
// frame-by-frame, decodes each packet, and asserts on the round-trip
// behavior. Opus is lossy, so tolerances are generous (peak amplitude
// within ±15 % of the encoded value, frame counts exact).
//
// Also exercises the PLC path (`decodePacket(nullptr, 0, ...)`).

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

#define EXPECT_LE(name, value, upper) do { \
    ++g_total; \
    const long _v = (long)(value); \
    const long _u = (long)(upper); \
    if (_v <= _u) { ++g_pass; Serial.print("PASS "); Serial.println(name); } \
    else { \
        Serial.print("FAIL "); Serial.print(name); \
        Serial.print(" value=");  Serial.print(_v); \
        Serial.print(" upper=");  Serial.println(_u); \
    } \
} while (0)

#define EXPECT_NEAR_REL(name, expected, actual, rel_pct) do { \
    ++g_total; \
    const long _e = (long)(expected); \
    const long _a = (long)(actual); \
    const long _d = (_e > _a) ? (_e - _a) : (_a - _e); \
    const long _tol = (long)((_e > 0 ? _e : -_e) * (rel_pct) / 100); \
    if (_d <= _tol) { ++g_pass; Serial.print("PASS "); Serial.println(name); } \
    else { \
        Serial.print("FAIL "); Serial.print(name); \
        Serial.print(" expected="); Serial.print(_e); \
        Serial.print(" actual=");   Serial.print(_a); \
        Serial.print(" diff=");     Serial.print(_d); \
        Serial.print(" tol=");      Serial.println(_tol); \
    } \
} while (0)


// Fills `pcm` with a 440 Hz sine at amplitude 16383 (50 % full-scale int16).
// `frameCount` is samples per channel. Stereo writes the same sine to both
// channels.
static void generate_sine(int16_t *pcm,
                          size_t frameCount,
                          uint32_t sampleRate,
                          uint8_t channels,
                          size_t phase_offset_samples)
{
    const float two_pi_f_over_sr = 2.0f * 3.14159265358979323846f * 440.0f
                                    / static_cast<float>(sampleRate);
    for (size_t i = 0; i < frameCount; ++i) {
        const float t = static_cast<float>(i + phase_offset_samples);
        const int16_t v = static_cast<int16_t>(16383.0f * sinf(two_pi_f_over_sr * t));
        for (uint8_t c = 0; c < channels; ++c) {
            pcm[i * channels + c] = v;
        }
    }
}


// Encode → decode one frame and return the peak |amplitude| of the
// decoded PCM (mono interleaving folded by taking the max across all
// channels at each sample position). Returns -1 on any failure.
static int peak_of_roundtrip(OpusEncoder &enc,
                             OpusDecoder &dec,
                             const int16_t *pcm_in,
                             size_t frames_in,
                             uint8_t channels)
{
    uint8_t pkt[400];
    const size_t n = enc.encodeFrame(pcm_in, frames_in, pkt, sizeof(pkt));
    if (n == 0) return -1;

    int16_t pcm_out[320 * 2];   // big enough for our largest frame in this test
    const size_t frames_out = dec.decodePacket(pkt, n, pcm_out, frames_in);
    if (frames_out != frames_in) return -1;

    int peak = 0;
    for (size_t i = 0; i < frames_out * channels; ++i) {
        const int v = pcm_out[i] >= 0 ? pcm_out[i] : -pcm_out[i];
        if (v > peak) peak = v;
    }
    return peak;
}


// --- VoIP-typical: 16 kHz mono, 20 ms frames, 24 kbps ---------------------
static void test_voip_16k_mono()
{
    constexpr uint32_t kRate     = 16000;
    constexpr uint8_t  kChannels = 1;
    constexpr size_t   kFrame    = 320;   // 20 ms @ 16 kHz

    OpusEncoder enc;
    OpusDecoder dec;
    EXPECT_TRUE("voip16/enc-begin",
                enc.begin({kRate, kChannels, 16}, OpusApplication::Voip, 24000));
    EXPECT_TRUE("voip16/dec-begin",
                dec.begin({kRate, kChannels, 16}));

    int16_t pcm[kFrame];

    // Opus output ramps up over the first ~80 ms; skip those frames and
    // measure on a steady-state frame.
    for (int f = 0; f < 4; ++f) {
        generate_sine(pcm, kFrame, kRate, kChannels, f * kFrame);
        uint8_t pkt[400];
        const size_t n = enc.encodeFrame(pcm, kFrame, pkt, sizeof(pkt));
        EXPECT_TRUE("voip16/warmup-encode", n > 0);
        int16_t scratch[kFrame];
        dec.decodePacket(pkt, n, scratch, kFrame);
    }

    generate_sine(pcm, kFrame, kRate, kChannels, 4 * kFrame);
    const int peak = peak_of_roundtrip(enc, dec, pcm, kFrame, kChannels);
    EXPECT_TRUE("voip16/decoded-non-empty", peak > 0);
    // Lossy ±15 %. Loose because Opus VoIP mode is quality-budget-driven.
    EXPECT_NEAR_REL("voip16/peak", 16383, peak, 15);

    // PLC path: synthesize one 20 ms frame from "lost" packet.
    int16_t plc[kFrame];
    const size_t plc_frames = dec.decodePacket(nullptr, 0, plc, kFrame);
    EXPECT_EQ("voip16/plc-frames", kFrame, plc_frames);

    enc.end();
    dec.end();
}


// --- Wideband: 48 kHz stereo, 20 ms, 64 kbps ------------------------------
static void test_audio_48k_stereo()
{
    constexpr uint32_t kRate     = 48000;
    constexpr uint8_t  kChannels = 2;
    constexpr size_t   kFrame    = 960;   // 20 ms @ 48 kHz

    OpusEncoder enc;
    OpusDecoder dec;
    EXPECT_TRUE("audio48/enc-begin",
                enc.begin({kRate, kChannels, 16}, OpusApplication::Audio, 64000));
    EXPECT_TRUE("audio48/dec-begin",
                dec.begin({kRate, kChannels, 16}));

    static int16_t pcm[kFrame * kChannels];

    // Warm up and capture peak on the 4th frame.
    int peak = 0;
    for (int f = 0; f < 5; ++f) {
        generate_sine(pcm, kFrame, kRate, kChannels, f * kFrame);
        uint8_t pkt[800];
        const size_t n = enc.encodeFrame(pcm, kFrame, pkt, sizeof(pkt));
        EXPECT_TRUE("audio48/encode", n > 0);
        EXPECT_LE("audio48/packet-bound", n, 1275);

        static int16_t out[kFrame * kChannels];
        const size_t got = dec.decodePacket(pkt, n, out, kFrame);
        EXPECT_EQ("audio48/decoded-frames", kFrame, got);
        if (f == 4) {
            for (size_t i = 0; i < got * kChannels; ++i) {
                const int v = out[i] >= 0 ? out[i] : -out[i];
                if (v > peak) peak = v;
            }
        }
    }
    EXPECT_NEAR_REL("audio48/peak", 16383, peak, 15);

    enc.end();
    dec.end();
}


// --- Bad inputs and edge cases --------------------------------------------
static void test_invalid_inputs()
{
    OpusEncoder enc;

    // Bad rate.
    EXPECT_TRUE("err/bad-rate",
                !enc.begin({11025, 1, 16}, OpusApplication::Voip, 24000));
    EXPECT_EQ("err/bad-rate-code",
              (int)OpusEncoder::Error::UnsupportedRate, (int)enc.lastError());

    // Bad channels.
    EXPECT_TRUE("err/bad-channels",
                !enc.begin({16000, 5, 16}, OpusApplication::Voip, 24000));
    EXPECT_EQ("err/bad-channels-code",
              (int)OpusEncoder::Error::UnsupportedChannels, (int)enc.lastError());

    // Bad bit depth.
    EXPECT_TRUE("err/bad-bits",
                !enc.begin({16000, 1, 8}, OpusApplication::Voip, 24000));
    EXPECT_EQ("err/bad-bits-code",
              (int)OpusEncoder::Error::InvalidFormat, (int)enc.lastError());

    // encodeFrame before begin() should fail gracefully.
    OpusEncoder fresh;
    int16_t pcm[160] = {0};
    uint8_t pkt[200];
    EXPECT_EQ("err/encode-not-ready", 0,
              fresh.encodeFrame(pcm, 160, pkt, sizeof(pkt)));

    // Mirror checks on the decoder.
    OpusDecoder dec;
    EXPECT_TRUE("err/dec-bad-rate", !dec.begin({22050, 1, 16}));
    EXPECT_TRUE("err/dec-bad-bits", !dec.begin({16000, 1, 24}));
}


void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start");

    test_voip_16k_mono();
    test_audio_48k_stereo();
    test_invalid_inputs();

    Serial.print("TEST done ");
    Serial.print(g_pass);
    Serial.print("/");
    Serial.println(g_total);
}


void loop()
{
    delay(1);
}
