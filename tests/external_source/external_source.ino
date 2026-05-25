// Verifies that OpusDecoder can be plugged into PCMFlow via
// `setInputSource()` for continuous packet-driven streaming.
//
// Each iteration of the per-packet loop:
//   1. encode one frame with OpusEncoder
//   2. queue PCM via OpusDecoder::decodePacket(pcm=nullptr, maxFrames=0)
//   3. drain the PCMFlow ring buffer until empty
//
// Between iterations the decoder's PCMSource::readFrames() returns 0;
// PCMFlow must treat that as "temporarily empty" (not EOF) and resume
// pulling on the next pump() — fixed in PCMFlow >= 0.2.1.

#include <PCMFlow.h>
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


// --- Multi-packet streaming: 16 kHz mono, 8 consecutive 20 ms frames -----
static void test_streaming_16k_mono()
{
    constexpr uint32_t kRate     = 16000;
    constexpr uint8_t  kChannels = 1;
    constexpr size_t   kFrame    = 320;   // 20 ms @ 16 kHz
    constexpr int      kFrames   = 8;     // 160 ms total

    OpusEncoder enc;
    OpusDecoder dec;
    PCMFlow audio;

    EXPECT_TRUE("voip16/enc-begin",
                enc.begin({kRate, kChannels, 16}, OpusApplication::Voip, 24000));
    EXPECT_TRUE("voip16/dec-begin", dec.begin({kRate, kChannels, 16}));

    audio.setInputSource(dec);
    audio.setOutputFormat({kRate, kChannels, 16});

    int16_t pcm[kFrame];
    int16_t out[kFrame];
    uint8_t pkt[400];
    size_t total_out = 0;

    for (int f = 0; f < kFrames; ++f) {
        fill_sine(pcm, kFrame, kRate, kChannels, f * kFrame);
        const size_t n = enc.encodeFrame(pcm, kFrame, pkt, sizeof(pkt));
        EXPECT_TRUE("voip16/encoded", n > 0);

        const size_t queued = dec.decodePacket(pkt, n, nullptr, 0);
        EXPECT_EQ("voip16/queued", (long)kFrame, (long)queued);

        // Drain everything this packet produced before encoding the next
        // one — the decoder's internal buffer overwrites on refill.
        while (true) {
            audio.pump();
            const size_t got = audio.readFrames(out, kFrame);
            if (got == 0) break;
            total_out += got;
        }
    }

    EXPECT_TRUE("voip16/audio-ready",  audio.isReady());
    EXPECT_EQ ("voip16/src-rate", (long)kRate,     (long)audio.sourceFormat().sampleRate);
    EXPECT_EQ ("voip16/src-ch",   (long)kChannels, (long)audio.sourceFormat().channels);
    EXPECT_EQ ("voip16/total-frames", (long)(kFrames * kFrame), (long)total_out);
}


// --- Multi-packet streaming: 48 kHz stereo, 5 consecutive 20 ms frames ---
static void test_streaming_48k_stereo()
{
    constexpr uint32_t kRate     = 48000;
    constexpr uint8_t  kChannels = 2;
    constexpr size_t   kFrame    = 960;   // 20 ms @ 48 kHz
    constexpr int      kFrames   = 5;     // 100 ms total

    OpusEncoder enc;
    OpusDecoder dec;
    PCMFlow audio;

    EXPECT_TRUE("audio48/enc-begin",
                enc.begin({kRate, kChannels, 16}, OpusApplication::Audio, 64000));
    EXPECT_TRUE("audio48/dec-begin", dec.begin({kRate, kChannels, 16}));

    audio.setInputSource(dec);
    audio.setOutputFormat({kRate, kChannels, 16});

    static int16_t pcm[kFrame * kChannels];
    static int16_t out[kFrame * kChannels];
    uint8_t pkt[800];
    size_t total_out = 0;

    for (int f = 0; f < kFrames; ++f) {
        fill_sine(pcm, kFrame, kRate, kChannels, f * kFrame);
        const size_t n = enc.encodeFrame(pcm, kFrame, pkt, sizeof(pkt));
        EXPECT_TRUE("audio48/encoded", n > 0);

        dec.decodePacket(pkt, n, nullptr, 0);

        while (true) {
            audio.pump();
            const size_t got = audio.readFrames(out, kFrame);
            if (got == 0) break;
            total_out += got;
        }
    }

    EXPECT_EQ("audio48/total-frames", (long)(kFrames * kFrame), (long)total_out);
}


// --- Resilience: temporarily empty source between packets ----------------
// Squeeze multiple `pump() -> 0` calls in between two decodePacket()
// calls; PCMFlow must NOT latch srcEof. This is the regression-guard
// for the PCMFlow 0.2.1 fix.
static void test_idle_pumps_between_packets()
{
    constexpr uint32_t kRate     = 16000;
    constexpr uint8_t  kChannels = 1;
    constexpr size_t   kFrame    = 320;

    OpusEncoder enc;
    OpusDecoder dec;
    PCMFlow audio;

    EXPECT_TRUE("idle/enc-begin",
                enc.begin({kRate, kChannels, 16}, OpusApplication::Voip, 24000));
    EXPECT_TRUE("idle/dec-begin", dec.begin({kRate, kChannels, 16}));
    audio.setInputSource(dec);
    audio.setOutputFormat({kRate, kChannels, 16});

    int16_t pcm[kFrame];
    int16_t out[kFrame];
    uint8_t pkt[400];

    // First packet through the pipeline.
    fill_sine(pcm, kFrame, kRate, kChannels, 0);
    const size_t n1 = enc.encodeFrame(pcm, kFrame, pkt, sizeof(pkt));
    dec.decodePacket(pkt, n1, nullptr, 0);
    size_t total_out = 0;
    while (true) {
        audio.pump();
        const size_t got = audio.readFrames(out, kFrame);
        if (got == 0) break;
        total_out += got;
    }
    EXPECT_EQ("idle/first-drained", (long)kFrame, (long)total_out);
    EXPECT_TRUE("idle/not-eof-after-1", !audio.isEof());

    // Several empty pump cycles — these would have latched srcEof on
    // pre-0.2.1 PCMFlow and made the next packet's audio invisible.
    for (int i = 0; i < 5; ++i) {
        audio.pump();
        EXPECT_TRUE("idle/still-not-eof", !audio.isEof());
    }

    // Second packet must flow through despite the idle gap.
    fill_sine(pcm, kFrame, kRate, kChannels, kFrame);
    const size_t n2 = enc.encodeFrame(pcm, kFrame, pkt, sizeof(pkt));
    dec.decodePacket(pkt, n2, nullptr, 0);
    size_t second_out = 0;
    while (true) {
        audio.pump();
        const size_t got = audio.readFrames(out, kFrame);
        if (got == 0) break;
        second_out += got;
    }
    EXPECT_EQ("idle/second-drained", (long)kFrame, (long)second_out);
}


void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start");

    test_streaming_16k_mono();
    test_streaming_48k_stereo();
    test_idle_pumps_between_packets();

    Serial.print("TEST done ");
    Serial.print(g_pass);
    Serial.print("/");
    Serial.println(g_total);
}


void loop()
{
    delay(1);
}
