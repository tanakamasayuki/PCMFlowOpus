// OpusDecoder implementation.
//
// Same name-collision dodge as in OpusEncoder.cpp: rename libopus's
// `OpusEncoder` / `OpusDecoder` typedefs via macros before including
// opus.h so they don't clash with our public class names.

#define OpusEncoder LibopusEncoderHandle
#define OpusDecoder LibopusDecoderHandle
extern "C" {
#include "external/opus/include/opus.h"
}
#undef OpusEncoder
#undef OpusDecoder

#include "OpusDecoder.h"

#include <new>
#include <string.h>


struct OpusDecoder::Impl
{
    LibopusDecoderHandle *dec = nullptr;

    // Internal PCM buffer used by the PCMSource path (readFrames). The
    // largest single Opus packet decodes to 60 ms @ 48 kHz = 2880 samples
    // per channel, stereo → 5760 int16 elements.
    int16_t  buf[5760] = {0};
    uint16_t buf_fill  = 0;   // samples-per-channel currently queued
    uint16_t buf_read  = 0;   // sample-per-channel read index into buf
};


namespace {

bool is_valid_opus_rate(uint32_t r)
{
    return r == 8000 || r == 12000 || r == 16000 || r == 24000 || r == 48000;
}

} // namespace


OpusDecoder::~OpusDecoder()
{
    end();
}


bool OpusDecoder::begin(const PCMFormat &outputFormat)
{
    end();

    if (outputFormat.bitsPerSample != 16)
    {
        error_ = Error::InvalidFormat;
        return false;
    }
    if (outputFormat.channels != 1 && outputFormat.channels != 2)
    {
        error_ = Error::UnsupportedChannels;
        return false;
    }
    if (!is_valid_opus_rate(outputFormat.sampleRate))
    {
        error_ = Error::UnsupportedRate;
        return false;
    }

    impl_ = new (std::nothrow) Impl();
    if (impl_ == nullptr)
    {
        error_ = Error::InitFailed;
        return false;
    }

    int err = OPUS_OK;
    impl_->dec = opus_decoder_create(
        static_cast<opus_int32>(outputFormat.sampleRate),
        outputFormat.channels,
        &err);
    if (impl_->dec == nullptr || err != OPUS_OK)
    {
        delete impl_;
        impl_ = nullptr;
        error_ = Error::InitFailed;
        return false;
    }

    format_ = outputFormat;
    ready_ = true;
    error_ = Error::None;
    return true;
}


void OpusDecoder::end()
{
    if (impl_ != nullptr)
    {
        if (impl_->dec != nullptr)
        {
            opus_decoder_destroy(impl_->dec);
        }
        delete impl_;
        impl_ = nullptr;
    }
    format_ = PCMFormat{};
    ready_ = false;
    error_ = Error::NotReady;
}


size_t OpusDecoder::decodeShared_(const uint8_t *packet,
                                  size_t packetBytes,
                                  int16_t *pcm,
                                  size_t maxFrames,
                                  int decode_fec)
{
    int16_t *out;
    int out_capacity;
    const bool route_internal = (pcm == nullptr && maxFrames == 0);
    if (route_internal)
    {
        out = impl_->buf;
        out_capacity = static_cast<int>(sizeof(impl_->buf) / sizeof(int16_t) / format_.channels);
        // Drop any unread leftover before refilling.
        impl_->buf_fill = 0;
        impl_->buf_read = 0;
    }
    else
    {
        out = pcm;
        out_capacity = static_cast<int>(maxFrames);
    }

    const opus_int32 n = opus_decode(
        impl_->dec,
        packet,
        static_cast<opus_int32>(packetBytes),
        out,
        out_capacity,
        decode_fec);
    if (n < 0)
    {
        error_ = Error::DecodeFailed;
        return 0;
    }

    if (route_internal)
    {
        impl_->buf_fill = static_cast<uint16_t>(n);
    }
    return static_cast<size_t>(n);
}


size_t OpusDecoder::decodePacket(const uint8_t *packet,
                                 size_t packetBytes,
                                 int16_t *pcm,
                                 size_t maxFrames)
{
    if (!ready_) { error_ = Error::NotReady; return 0; }
    return decodeShared_(packet, packetBytes, pcm, maxFrames, /*decode_fec=*/0);
}


size_t OpusDecoder::decodePacketFec(const uint8_t *packet,
                                    size_t packetBytes,
                                    int16_t *pcm,
                                    size_t maxFrames)
{
    if (!ready_) { error_ = Error::NotReady; return 0; }
    return decodeShared_(packet, packetBytes, pcm, maxFrames, /*decode_fec=*/1);
}


size_t OpusDecoder::readFrames(void *out, size_t frameCount)
{
    if (!ready_ || out == nullptr || frameCount == 0)
        return 0;

    const uint16_t avail = impl_->buf_fill - impl_->buf_read;
    if (avail == 0)
        return 0;

    const size_t take = frameCount < avail ? frameCount : avail;
    memcpy(out,
           &impl_->buf[impl_->buf_read * format_.channels],
           take * format_.channels * sizeof(int16_t));
    impl_->buf_read = static_cast<uint16_t>(impl_->buf_read + take);
    if (impl_->buf_read >= impl_->buf_fill)
    {
        impl_->buf_fill = 0;
        impl_->buf_read = 0;
    }
    return take;
}
