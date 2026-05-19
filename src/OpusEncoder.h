#ifndef PCMFLOWOPUS_OPUSENCODER_H
#define PCMFLOWOPUS_OPUSENCODER_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

#include "PCMFormat.h"
#include "PCMSink.h"

// PCMFlowOpus :: OpusEncoder
//
// Wraps libopus's `OpusEncoder` (vendored under src/external/opus) and
// exposes it through PCMFlow's PCMSink interface. Accepts 16-bit signed
// interleaved PCM, emits raw Opus packets ready for ESP-NOW / RTP / UDP /
// WebSocket transport.
//
// Operates on one Opus frame at a time. The caller is responsible for
// chunking input PCM to the encoder's expected frame size (rate *
// frameDurationMs / 1000 samples per channel).
//
// The libopus type is hidden behind a pImpl so users do not transitively
// see `opus.h` through PCMFlow's public surface.
//
// STATUS: header skeleton only. Implementation is forthcoming.
//         Signatures below are SPEC §4.1 sketches and may shift slightly
//         once the underlying error semantics are mapped.

enum class OpusApplication : uint8_t
{
    Voip,      // OPUS_APPLICATION_VOIP        — speech, narrow/wideband, max ploss resilience
    Audio,     // OPUS_APPLICATION_AUDIO       — general audio, music
    LowDelay,  // OPUS_APPLICATION_RESTRICTED_LOWDELAY — minimum codec delay, no SILK
};

class OpusEncoder : public PCMSink
{
public:
    enum class Error : uint8_t
    {
        None,
        NotReady,
        InitFailed,
        InvalidFormat,
        UnsupportedChannels,
        UnsupportedRate,
        EncodeFailed,
    };

    OpusEncoder() = default;
    ~OpusEncoder();

    OpusEncoder(const OpusEncoder &) = delete;
    OpusEncoder &operator=(const OpusEncoder &) = delete;

    // Initialize the underlying libopus encoder.
    //   inputFormat : sampleRate must be 8000/12000/16000/24000/48000;
    //                 channels 1 or 2; bitsPerSample == 16.
    //   application : see OpusApplication.
    //   bitrate_bps : target bitrate. 6000 .. 510000.
    // Returns false on error; query lastError() for the cause.
    bool begin(const PCMFormat &inputFormat,
               OpusApplication application,
               int bitrate_bps);
    void end();

    // Runtime tuning. May be called any time after a successful begin().
    bool setBitrate(int bps);
    bool setComplexity(int level0_to_10);
    bool setFrameDuration(uint8_t milliseconds);   // 3, 5, 10, 20, 40, 60 (2.5 ms passed as 3 by convention)
    bool setDtx(bool enabled);
    bool setInbandFec(bool enabled);
    bool setPacketLossPerc(int perc0_to_100);

    // Encode exactly one Opus frame's worth of PCM samples (interleaved
    // for stereo). `frameCount` must equal the configured frame size
    // (samples per channel). Returns the number of bytes written into
    // `packet`, or 0 on error.
    size_t encodeFrame(const int16_t *pcm,
                       size_t frameCount,
                       uint8_t *packet,
                       size_t packetCapacity);

    // PCMSink interface --------------------------------------------------
    // Pushes PCM into an internal accumulator. When a full Opus frame's
    // worth of samples is buffered, the encoder produces a packet and
    // hands it to the registered packet sink. Without a sink set the
    // PCMSink path is a no-op; use encodeFrame() for direct control.
    const PCMFormat &format() const override { return format_; }
    bool isReady() const override { return ready_; }
    size_t writeFrames(const void *in, size_t frameCount) override;

    Error lastError() const { return error_; }

    // Packet sink callback signature for the PCMSink path.
    using PacketSink = void (*)(void *userData,
                                const uint8_t *packet,
                                size_t packetBytes);
    void setPacketSink(PacketSink cb, void *userData);

private:
    struct Impl;

    Impl *impl_ = nullptr;
    PCMFormat format_{};
    bool ready_ = false;
    Error error_ = Error::NotReady;

    PacketSink packetSinkCb_ = nullptr;
    void *packetSinkUser_ = nullptr;
};

#endif // PCMFLOWOPUS_OPUSENCODER_H
