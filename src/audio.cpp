// snd::audio implementation over miniaudio. The single MINIAUDIO_IMPLEMENTATION
// for the whole library lives here.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "snd/audio.h"

#include <atomic>
#include <cstring>

namespace snd::audio {

// ---------------------------------------------------------------------------
// load / save
// ---------------------------------------------------------------------------

bool load(const std::string& path, Buffer& out, std::string* error)
{
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS) {
        if (error) *error = "could not open/decode: " + path;
        return false;
    }

    out.channels = decoder.outputChannels;
    out.sampleRate = decoder.outputSampleRate;
    out.samples.clear();

    float chunk[4096];
    const ma_uint64 chunkFrames = 4096 / decoder.outputChannels;
    for (;;) {
        ma_uint64 read = 0;
        ma_result r = ma_decoder_read_pcm_frames(&decoder, chunk, chunkFrames, &read);
        if (read > 0)
            out.samples.insert(out.samples.end(), chunk, chunk + read * decoder.outputChannels);
        if (r != MA_SUCCESS || read < chunkFrames)
            break;
    }

    ma_decoder_uninit(&decoder);
    if (out.samples.empty()) {
        if (error) *error = "decoded zero frames: " + path;
        return false;
    }
    return true;
}

bool saveWav(const std::string& path, const Buffer& buf, std::string* error)
{
    if (buf.channels == 0 || buf.sampleRate == 0 || buf.samples.empty()) {
        if (error) *error = "empty buffer";
        return false;
    }

    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32,
                                                   buf.channels, buf.sampleRate);
    ma_encoder encoder;
    if (ma_encoder_init_file(path.c_str(), &cfg, &encoder) != MA_SUCCESS) {
        if (error) *error = "could not open for writing: " + path;
        return false;
    }

    ma_uint64 written = 0;
    ma_result r = ma_encoder_write_pcm_frames(&encoder, buf.samples.data(), buf.frames(), &written);
    ma_encoder_uninit(&encoder);

    if (r != MA_SUCCESS || written != buf.frames()) {
        if (error) *error = "short write: " + path;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

struct Device::Impl {
    ma_device device{};
    Callback callback;
    bool open = false;

    static void maCallback(ma_device* dev, void* output, const void* /*input*/, ma_uint32 frames)
    {
        auto* self = (Impl*)dev->pUserData;
        auto channels = dev->playback.channels;
        if (self->callback)
            self->callback((float*)output, frames, channels);
        else
            ma_silence_pcm_frames(output, frames, ma_format_f32, channels);
    }
};

Device::Device() : impl(new Impl) {}
Device::~Device() { close(); }

bool Device::open(uint32_t sampleRate, uint32_t channels, Callback cb)
{
    close();
    impl->callback = std::move(cb);

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = channels;
    cfg.sampleRate = sampleRate;
    cfg.dataCallback = Impl::maCallback;
    cfg.pUserData = impl.get();

    if (ma_device_init(nullptr, &cfg, &impl->device) != MA_SUCCESS)
        return false;
    impl->open = true;
    return true;
}

void Device::close()
{
    if (impl->open) {
        ma_device_uninit(&impl->device);
        impl->open = false;
    }
}

bool Device::start() { return impl->open && ma_device_start(&impl->device) == MA_SUCCESS; }
void Device::stop() { if (impl->open) ma_device_stop(&impl->device); }
bool Device::isOpen() const { return impl->open; }

std::string Device::name() const { return impl->open ? impl->device.playback.name : ""; }
uint32_t Device::sampleRate() const { return impl->open ? impl->device.sampleRate : 0; }
uint32_t Device::channels() const { return impl->open ? impl->device.playback.channels : 0; }

// ---------------------------------------------------------------------------
// CaptureDevice
// ---------------------------------------------------------------------------

struct CaptureDevice::Impl {
    ma_device device{};
    Callback callback;
    bool open = false;

    static void maCallback(ma_device* dev, void* /*output*/, const void* input, ma_uint32 frames)
    {
        auto* self = (Impl*)dev->pUserData;
        if (self->callback)
            self->callback((const float*)input, frames, dev->capture.channels);
    }
};

CaptureDevice::CaptureDevice() : impl(new Impl) {}
CaptureDevice::~CaptureDevice() { close(); }

bool CaptureDevice::open(uint32_t sampleRate, uint32_t channels, Callback cb)
{
    close();
    impl->callback = std::move(cb);

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = channels;
    cfg.sampleRate = sampleRate;
    cfg.dataCallback = Impl::maCallback;
    cfg.pUserData = impl.get();

    if (ma_device_init(nullptr, &cfg, &impl->device) != MA_SUCCESS)
        return false;
    impl->open = true;
    return true;
}

void CaptureDevice::close()
{
    if (impl->open) {
        ma_device_uninit(&impl->device);
        impl->open = false;
    }
}

bool CaptureDevice::start() { return impl->open && ma_device_start(&impl->device) == MA_SUCCESS; }
void CaptureDevice::stop() { if (impl->open) ma_device_stop(&impl->device); }

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------

struct Player::Impl {
    Device device;
    const Buffer* buffer = nullptr;         // set from main thread while stopped
    std::atomic<uint64_t> position{0};
    std::atomic<uint64_t> endFrame{0};
    std::atomic<bool> playing{false};

    void render(float* out, uint32_t frames, uint32_t channels)
    {
        std::memset(out, 0, sizeof(float) * frames * channels);
        if (!playing.load(std::memory_order_relaxed) || !buffer)
            return;

        const Buffer& b = *buffer;
        uint64_t pos = position.load(std::memory_order_relaxed);
        const uint64_t end = std::min<uint64_t>(endFrame.load(std::memory_order_relaxed), b.frames());

        for (uint32_t f = 0; f < frames && pos < end; ++f, ++pos) {
            for (uint32_t c = 0; c < channels; ++c) {
                uint32_t src = c < b.channels ? c : b.channels - 1;
                out[f * channels + c] = b.samples[pos * b.channels + src];
            }
        }
        position.store(pos, std::memory_order_relaxed);
        if (pos >= end)
            playing.store(false, std::memory_order_relaxed);
    }
};

Player::Player() : impl(new Impl) {}
Player::~Player() { close(); }

bool Player::open(uint32_t sampleRate, uint32_t channels)
{
    auto* p = impl.get();
    if (!impl->device.open(sampleRate, channels,
                           [p](float* out, uint32_t frames, uint32_t ch) { p->render(out, frames, ch); }))
        return false;
    return impl->device.start();
}

void Player::close() { impl->device.close(); }

void Player::setBuffer(const Buffer* buf)
{
    impl->playing.store(false);
    impl->buffer = buf;
    impl->position.store(0);
    impl->endFrame.store(buf ? buf->frames() : 0);
}

void Player::play()
{
    if (!impl->buffer) return;
    if (impl->position.load() >= impl->buffer->frames())
        impl->position.store(0);
    impl->endFrame.store(impl->buffer->frames());
    impl->playing.store(true);
}

void Player::playRange(uint64_t startFrame, uint64_t endFrame)
{
    if (!impl->buffer) return;
    impl->position.store(startFrame);
    impl->endFrame.store(endFrame);
    impl->playing.store(true);
}

void Player::stop() { impl->playing.store(false); }
bool Player::isPlaying() const { return impl->playing.load(); }
uint64_t Player::positionFrames() const { return impl->position.load(); }
void Player::seek(uint64_t frame) { impl->position.store(frame); }

} // namespace snd::audio
