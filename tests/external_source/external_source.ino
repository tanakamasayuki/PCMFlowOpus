// Verifies that OpusDecoder can be plugged into PCMFlow via
// `setInputSource()`. The test focuses on the SINGLE-PACKET case:
// encode one frame → decoder.decodePacket(pcm=nullptr) → drive
// audio.pump() + audio.readFrames() to drain the pipeline.
//
// Note on multi-packet streaming:
//   PCMFlow::processChunk() latches `srcEof_ = true` the first time
//   `source->readFrames()` returns 0, regardless of `source->isEof()`.
//   That makes the pull-model PCMSource interface incompatible with
//   streaming/packet-driven sources that legitimately return 0 between
//   packets. The parent library needs a one-line fix
//   (check `source->isEof()` before latching `srcEof_`) before this
//   integration is robust for continuous VoIP use. Until then, callers
//   that need ongoing streaming should drive decodePacket() with an
//   explicit PCM output buffer and feed that buffer to the application
//   directly, bypassing PCMFlow's setInputSource hook.

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


static void fill_sine(int16_t *pcm, size_t frames, uint32_t rate, uint8_t channels)
{
    const float w = 2.0f * 3.14159265f * 440.0f / static_cast<float>(rate);
    for (size_t i = 0; i < frames; ++i) {
        const int16_t v = static_cast<int16_t>(16383.0f * sinf(w * static_cast<float>(i)));
        for (uint8_t c = 0; c < channels; ++c) pcm[i * channels + c] = v;
    }
}


// Encode 1 frame at the given format and run it end-to-end through
// PCMFlow's setInputSource pipeline. Verifies that exactly `frame`
// frames come out and that PCMFlow's source-format reporting matches
// what the OpusDecoder advertises.
static void test_single_packet(const char *tag,
                               uint32_t rate,
                               uint8_t channels,
                               size_t frame)
{
    OpusEncoder enc;
    OpusDecoder dec;
    PCMFlow audio;

    {
        char n[40]; snprintf(n, sizeof(n), "%s/enc-begin", tag);
        EXPECT_TRUE(n, enc.begin({rate, channels, 16},
                                 channels == 2 ? OpusApplication::Audio : OpusApplication::Voip,
                                 channels == 2 ? 64000 : 24000));
    }
    {
        char n[40]; snprintf(n, sizeof(n), "%s/dec-begin", tag);
        EXPECT_TRUE(n, dec.begin({rate, channels, 16}));
    }

    audio.setInputSource(dec);
    audio.setOutputFormat({rate, channels, 16});

    static int16_t pcm[960 * 2];
    static int16_t out[960 * 2];
    uint8_t pkt[800];

    fill_sine(pcm, frame, rate, channels);
    const size_t n = enc.encodeFrame(pcm, frame, pkt, sizeof(pkt));
    {
        char nm[40]; snprintf(nm, sizeof(nm), "%s/encoded", tag);
        EXPECT_TRUE(nm, n > 0);
    }

    const size_t queued = dec.decodePacket(pkt, n, nullptr, 0);
    {
        char nm[40]; snprintf(nm, sizeof(nm), "%s/queued", tag);
        EXPECT_EQ(nm, (long)frame, (long)queued);
    }

    size_t total_out = 0;
    while (true) {
        audio.pump();
        const size_t got = audio.readFrames(out, frame);
        if (got == 0) break;
        total_out += got;
    }

    {
        char nm[40]; snprintf(nm, sizeof(nm), "%s/src-rate", tag);
        EXPECT_EQ(nm, (long)rate, (long)audio.sourceFormat().sampleRate);
    }
    {
        char nm[40]; snprintf(nm, sizeof(nm), "%s/src-channels", tag);
        EXPECT_EQ(nm, (long)channels, (long)audio.sourceFormat().channels);
    }
    {
        char nm[40]; snprintf(nm, sizeof(nm), "%s/total-frames", tag);
        EXPECT_EQ(nm, (long)frame, (long)total_out);
    }
}


void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start");

    test_single_packet("voip16",  16000, 1, 320);   // 20 ms @ 16 kHz mono
    test_single_packet("audio48", 48000, 2, 960);   // 20 ms @ 48 kHz stereo

    Serial.print("TEST done ");
    Serial.print(g_pass);
    Serial.print("/");
    Serial.println(g_total);
}


void loop()
{
    delay(1);
}
