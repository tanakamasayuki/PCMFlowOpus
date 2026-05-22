// OpusEncoder implementation.
//
// The libopus C API uses an opaque type also called `OpusEncoder`, which
// would collide with our public class of the same name. We work around it
// by renaming the libopus typedef via a localized preprocessor macro
// BEFORE including opus.h, and undefining it immediately after. The
// libopus headers don't reference the typedef in any way that survives
// the include, so the rename is safe and contained to this translation
// unit.
//
// libopus is included with `extern "C"` because its headers are C with
// no C++ guard.

#define OpusEncoder LibopusEncoderHandle
#define OpusDecoder LibopusDecoderHandle
extern "C" {
#include "external/opus/include/opus.h"
}
#undef OpusEncoder
#undef OpusDecoder

#include "OpusEncoder.h"

#include <new>
#include <string.h>


struct OpusEncoder::Impl
{
    LibopusEncoderHandle *enc = nullptr;

    // PCMSink accumulator: how many samples-per-channel make up one Opus
    // frame, what we've buffered so far, and the storage. Max Opus frame
    // is 60 ms @ 48 kHz = 2880 samples per channel; stereo doubles the
    // interleaved sample count, hence 2880 * 2 = 5760 int16 elements.
    uint16_t frame_samples = 0;      // samples per channel per Opus frame
    uint16_t buf_fill = 0;           // samples-per-channel currently held
    int16_t  buf[5760] = {0};        // interleaved PCM accumulator

    // Reusable packet output buffer for the PCMSink path. 1275 bytes is
    // the Opus packet upper bound.
    uint8_t  pkt[1275] = {0};
};


namespace {

bool is_valid_opus_rate(uint32_t r)
{
    return r == 8000 || r == 12000 || r == 16000 || r == 24000 || r == 48000;
}

int map_application(OpusApplication app)
{
    switch (app)
    {
    case OpusApplication::Voip:     return OPUS_APPLICATION_VOIP;
    case OpusApplication::Audio:    return OPUS_APPLICATION_AUDIO;
    case OpusApplication::LowDelay: return OPUS_APPLICATION_RESTRICTED_LOWDELAY;
    }
    return OPUS_APPLICATION_VOIP;
}

// Map a frame duration in milliseconds to libopus's OPUS_FRAMESIZE_* enum.
// libopus treats 2.5 ms as a special case — by convention the caller
// passes 3 ms when they actually mean 2.5 ms (see SPEC §4.1).
int map_frame_duration(uint8_t ms)
{
    switch (ms)
    {
    case 3:  return OPUS_FRAMESIZE_2_5_MS;
    case 5:  return OPUS_FRAMESIZE_5_MS;
    case 10: return OPUS_FRAMESIZE_10_MS;
    case 20: return OPUS_FRAMESIZE_20_MS;
    case 40: return OPUS_FRAMESIZE_40_MS;
    case 60: return OPUS_FRAMESIZE_60_MS;
    }
    return -1;
}

// Samples per channel for a given frame duration at a given rate. Returns
// 0 for invalid combinations.
uint16_t samples_for_duration(uint32_t rate, uint8_t ms)
{
    // Special case: 2.5 ms encoded as the "3" sentinel.
    if (ms == 3) return static_cast<uint16_t>(rate * 25 / 10000);
    if (ms == 5 || ms == 10 || ms == 20 || ms == 40 || ms == 60)
        return static_cast<uint16_t>(rate * ms / 1000);
    return 0;
}

} // namespace


OpusEncoder::~OpusEncoder()
{
    end();
}


bool OpusEncoder::begin(const PCMFormat &inputFormat,
                        OpusApplication application,
                        int bitrate_bps)
{
    end();

    if (inputFormat.bitsPerSample != 16)
    {
        error_ = Error::InvalidFormat;
        return false;
    }
    if (inputFormat.channels != 1 && inputFormat.channels != 2)
    {
        error_ = Error::UnsupportedChannels;
        return false;
    }
    if (!is_valid_opus_rate(inputFormat.sampleRate))
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
    impl_->enc = opus_encoder_create(
        static_cast<opus_int32>(inputFormat.sampleRate),
        inputFormat.channels,
        map_application(application),
        &err);
    if (impl_->enc == nullptr || err != OPUS_OK)
    {
        delete impl_;
        impl_ = nullptr;
        error_ = Error::InitFailed;
        return false;
    }

    if (opus_encoder_ctl(impl_->enc, OPUS_SET_BITRATE(bitrate_bps)) != OPUS_OK)
    {
        opus_encoder_destroy(impl_->enc);
        delete impl_;
        impl_ = nullptr;
        error_ = Error::InitFailed;
        return false;
    }

    format_ = inputFormat;
    // PCMSink path default: 20 ms frames (VoIP convention). The caller can
    // override via setFrameDuration() before starting to stream PCM.
    impl_->frame_samples = samples_for_duration(inputFormat.sampleRate, 20);
    impl_->buf_fill = 0;

    ready_ = true;
    error_ = Error::None;
    return true;
}


void OpusEncoder::end()
{
    if (impl_ != nullptr)
    {
        if (impl_->enc != nullptr)
        {
            opus_encoder_destroy(impl_->enc);
        }
        delete impl_;
        impl_ = nullptr;
    }
    format_ = PCMFormat{};
    ready_ = false;
    error_ = Error::NotReady;
    packetSinkCb_ = nullptr;
    packetSinkUser_ = nullptr;
}


bool OpusEncoder::setBitrate(int bps)
{
    if (!ready_) { error_ = Error::NotReady; return false; }
    return opus_encoder_ctl(impl_->enc, OPUS_SET_BITRATE(bps)) == OPUS_OK;
}

bool OpusEncoder::setComplexity(int level0_to_10)
{
    if (!ready_) { error_ = Error::NotReady; return false; }
    return opus_encoder_ctl(impl_->enc, OPUS_SET_COMPLEXITY(level0_to_10)) == OPUS_OK;
}

bool OpusEncoder::setFrameDuration(uint8_t milliseconds)
{
    if (!ready_) { error_ = Error::NotReady; return false; }
    const int code = map_frame_duration(milliseconds);
    if (code < 0) return false;
    const uint16_t samples = samples_for_duration(format_.sampleRate, milliseconds);
    if (samples == 0) return false;
    if (opus_encoder_ctl(impl_->enc, OPUS_SET_EXPERT_FRAME_DURATION(code)) != OPUS_OK)
        return false;
    impl_->frame_samples = samples;
    impl_->buf_fill = 0;
    return true;
}

bool OpusEncoder::setDtx(bool enabled)
{
    if (!ready_) { error_ = Error::NotReady; return false; }
    return opus_encoder_ctl(impl_->enc, OPUS_SET_DTX(enabled ? 1 : 0)) == OPUS_OK;
}

bool OpusEncoder::setInbandFec(bool enabled)
{
    if (!ready_) { error_ = Error::NotReady; return false; }
    return opus_encoder_ctl(impl_->enc, OPUS_SET_INBAND_FEC(enabled ? 1 : 0)) == OPUS_OK;
}

bool OpusEncoder::setPacketLossPerc(int perc0_to_100)
{
    if (!ready_) { error_ = Error::NotReady; return false; }
    return opus_encoder_ctl(impl_->enc, OPUS_SET_PACKET_LOSS_PERC(perc0_to_100)) == OPUS_OK;
}


size_t OpusEncoder::encodeFrame(const int16_t *pcm,
                                size_t frameCount,
                                uint8_t *packet,
                                size_t packetCapacity)
{
    if (!ready_ || pcm == nullptr || packet == nullptr || packetCapacity == 0)
    {
        error_ = ready_ ? Error::EncodeFailed : Error::NotReady;
        return 0;
    }
    const opus_int32 n = opus_encode(
        impl_->enc,
        pcm,
        static_cast<int>(frameCount),
        packet,
        static_cast<opus_int32>(packetCapacity));
    if (n < 0)
    {
        error_ = Error::EncodeFailed;
        return 0;
    }
    return static_cast<size_t>(n);
}


void OpusEncoder::setPacketSink(PacketSink cb, void *userData)
{
    packetSinkCb_ = cb;
    packetSinkUser_ = userData;
}


size_t OpusEncoder::writeFrames(const void *in, size_t frameCount)
{
    if (!ready_ || in == nullptr || impl_->frame_samples == 0)
        return 0;

    const int16_t *src = static_cast<const int16_t *>(in);
    const uint8_t channels = format_.channels;
    size_t consumed = 0;

    while (consumed < frameCount)
    {
        const uint16_t want = impl_->frame_samples - impl_->buf_fill;
        const size_t avail = frameCount - consumed;
        const uint16_t take = static_cast<uint16_t>(avail < want ? avail : want);

        memcpy(&impl_->buf[impl_->buf_fill * channels],
               &src[consumed * channels],
               static_cast<size_t>(take) * channels * sizeof(int16_t));
        impl_->buf_fill = static_cast<uint16_t>(impl_->buf_fill + take);
        consumed += take;

        if (impl_->buf_fill == impl_->frame_samples)
        {
            const opus_int32 n = opus_encode(
                impl_->enc,
                impl_->buf,
                impl_->frame_samples,
                impl_->pkt,
                static_cast<opus_int32>(sizeof(impl_->pkt)));
            impl_->buf_fill = 0;
            if (n > 0 && packetSinkCb_ != nullptr)
            {
                packetSinkCb_(packetSinkUser_, impl_->pkt, static_cast<size_t>(n));
            }
            else if (n < 0)
            {
                error_ = Error::EncodeFailed;
                return consumed;
            }
        }
    }
    return consumed;
}
