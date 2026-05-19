#ifndef PCMFLOWOPUS_OPUSDECODER_H
#define PCMFLOWOPUS_OPUSDECODER_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

#include "PCMFormat.h"
#include "PCMSource.h"

// PCMFlowOpus :: OpusDecoder
//
// Wraps libopus's `OpusDecoder` (vendored under src/external/opus) and
// exposes it through PCMFlow's PCMSource interface. Accepts raw Opus
// packets (e.g. from ESP-NOW / RTP / UDP / WebSocket) and produces 16-bit
// signed interleaved PCM.
//
// Two non-standard read paths beyond plain decode:
//   - PLC : decodePacket(nullptr, 0, ...) synthesizes the missing frame
//   - FEC : decodePacketFec(...) reconstructs the previous frame from
//           the in-band FEC of the current packet
//
// The libopus type is hidden behind a pImpl.
//
// STATUS: header skeleton only. Implementation is forthcoming.
//         Signatures below are SPEC §4.2 sketches.

class OpusDecoder : public PCMSource
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
        DecodeFailed,
    };

    OpusDecoder() = default;
    ~OpusDecoder();

    OpusDecoder(const OpusDecoder &) = delete;
    OpusDecoder &operator=(const OpusDecoder &) = delete;

    // Initialize the underlying libopus decoder.
    //   outputFormat : sampleRate must be 8000/12000/16000/24000/48000;
    //                  channels 1 or 2; bitsPerSample == 16.
    bool begin(const PCMFormat &outputFormat);
    void end();

    // Decode one Opus packet. Pass packet=nullptr, packetBytes=0 to
    // engage PLC. `pcm` may be nullptr together with maxFrames=0 if the
    // caller routes output through the PCMSource path (queued internally
    // for later readFrames()).
    // Returns the number of PCM frames produced.
    size_t decodePacket(const uint8_t *packet,
                        size_t packetBytes,
                        int16_t *pcm,
                        size_t maxFrames);

    // Decode the previous packet from the current packet's in-band FEC
    // redundancy. The current packet's PCM is NOT produced by this call.
    size_t decodePacketFec(const uint8_t *packet,
                           size_t packetBytes,
                           int16_t *pcm,
                           size_t maxFrames);

    // PCMSource interface ------------------------------------------------
    // Returns frames previously queued by decodePacket()/decodePacketFec()
    // when the caller routes output through the PCMSource path.
    const PCMFormat &format() const override { return format_; }
    bool isReady() const override { return ready_; }
    bool isEof() const override { return false; }   // packet stream has no defined EOF
    size_t readFrames(void *out, size_t frameCount) override;

    Error lastError() const { return error_; }

private:
    struct Impl;

    Impl *impl_ = nullptr;
    PCMFormat format_{};
    bool ready_ = false;
    Error error_ = Error::NotReady;
};

#endif // PCMFLOWOPUS_OPUSDECODER_H
