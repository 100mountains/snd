// snd::audio implementation over miniaudio. The single MINIAUDIO_IMPLEMENTATION
// for the whole library lives here.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "snd/audio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace snd::audio {

// ---------------------------------------------------------------------------
// shared context + device enumeration (main thread only)
// ---------------------------------------------------------------------------

namespace {

ma_context* globalContext()
{
    static struct Holder {
        ma_context ctx{};
        bool ok = false;
        Holder() { ok = ma_context_init(nullptr, 0, nullptr, &ctx) == MA_SUCCESS; }
    } holder;
    return holder.ok ? &holder.ctx : nullptr;
}

// Look up a device id by name. Returns nullptr (= default) when not found or
// name is empty. The pointed-to id stays valid until the next enumeration.
const ma_device_id* findDeviceId(bool playback, const std::string& name)
{
    if (name.empty())
        return nullptr;
    ma_context* ctx = globalContext();
    if (!ctx)
        return nullptr;
    ma_device_info* playbackInfos = nullptr;
    ma_device_info* captureInfos = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    if (ma_context_get_devices(ctx, &playbackInfos, &playbackCount, &captureInfos,
                               &captureCount) != MA_SUCCESS)
        return nullptr;
    ma_device_info* infos = playback ? playbackInfos : captureInfos;
    ma_uint32 count = playback ? playbackCount : captureCount;
    for (ma_uint32 i = 0; i < count; ++i)
        if (name == infos[i].name)
            return &infos[i].id;
    return nullptr;
}

std::vector<std::string> deviceNames(bool playback)
{
    std::vector<std::string> out;
    ma_context* ctx = globalContext();
    if (!ctx)
        return out;
    ma_device_info* playbackInfos = nullptr;
    ma_device_info* captureInfos = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    if (ma_context_get_devices(ctx, &playbackInfos, &playbackCount, &captureInfos,
                               &captureCount) != MA_SUCCESS)
        return out;
    ma_device_info* infos = playback ? playbackInfos : captureInfos;
    ma_uint32 count = playback ? playbackCount : captureCount;
    for (ma_uint32 i = 0; i < count; ++i)
        out.push_back(infos[i].name);
    return out;
}

} // namespace

std::vector<std::string> playbackDevices() { return deviceNames(true); }
std::vector<std::string> captureDevices() { return deviceNames(false); }

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

bool resample(const Buffer& in, uint32_t newRate, Buffer& out, std::string* error)
{
    if (in.channels == 0 || in.sampleRate == 0 || in.samples.empty()) {
        if (error) *error = "empty buffer";
        return false;
    }
    if (newRate == in.sampleRate) {
        out = in;
        return true;
    }

    ma_resampler_config cfg = ma_resampler_config_init(
        ma_format_f32, in.channels, in.sampleRate, newRate, ma_resample_algorithm_linear);
    // linear with high lpf order is miniaudio's best built-in quality
    cfg.linear.lpfOrder = MA_MAX_FILTER_ORDER;

    ma_resampler resampler;
    if (ma_resampler_init(&cfg, nullptr, &resampler) != MA_SUCCESS) {
        if (error) *error = "resampler init failed";
        return false;
    }

    out.channels = in.channels;
    out.sampleRate = newRate;
    ma_uint64 expectedOut = 0;
    ma_resampler_get_expected_output_frame_count(&resampler, in.frames(), &expectedOut);
    out.samples.assign((size_t)(expectedOut + 16) * in.channels, 0.0f);

    ma_uint64 framesIn = in.frames();
    ma_uint64 framesOut = expectedOut + 16;
    ma_result r = ma_resampler_process_pcm_frames(&resampler, in.samples.data(), &framesIn,
                                                  out.samples.data(), &framesOut);
    ma_resampler_uninit(&resampler, nullptr);

    if (r != MA_SUCCESS) {
        if (error) *error = "resample failed";
        return false;
    }
    out.samples.resize((size_t)framesOut * out.channels);
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

bool Device::open(uint32_t sampleRate, uint32_t channels, Callback cb,
                  const std::string& deviceName)
{
    close();
    impl->callback = std::move(cb);

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = channels;
    cfg.playback.pDeviceID = findDeviceId(true, deviceName);
    cfg.sampleRate = sampleRate;
    cfg.dataCallback = Impl::maCallback;
    cfg.pUserData = impl.get();

    if (ma_device_init(globalContext(), &cfg, &impl->device) != MA_SUCCESS)
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

bool CaptureDevice::open(uint32_t sampleRate, uint32_t channels, Callback cb,
                         const std::string& deviceName)
{
    close();
    impl->callback = std::move(cb);

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = channels;
    cfg.capture.pDeviceID = findDeviceId(false, deviceName);
    cfg.sampleRate = sampleRate;
    cfg.dataCallback = Impl::maCallback;
    cfg.pUserData = impl.get();

    if (ma_device_init(globalContext(), &cfg, &impl->device) != MA_SUCCESS)
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
    std::atomic<uint64_t> rangeStart{0};
    std::atomic<uint64_t> endFrame{0};
    std::atomic<bool> playing{false};
    std::atomic<bool> looping{false};
    std::atomic<float> peaks[2] = {0.0f, 0.0f};

    void render(float* out, uint32_t frames, uint32_t channels)
    {
        std::memset(out, 0, sizeof(float) * frames * channels);
        if (!playing.load(std::memory_order_relaxed) || !buffer) {
            peaks[0].store(0.0f, std::memory_order_relaxed);
            peaks[1].store(0.0f, std::memory_order_relaxed);
            return;
        }

        const Buffer& b = *buffer;
        uint64_t pos = position.load(std::memory_order_relaxed);
        const uint64_t start = rangeStart.load(std::memory_order_relaxed);
        const uint64_t end =
            std::min<uint64_t>(endFrame.load(std::memory_order_relaxed), b.frames());
        const bool loop = looping.load(std::memory_order_relaxed);

        float pk[2] = {0.0f, 0.0f};
        for (uint32_t f = 0; f < frames; ++f) {
            if (pos >= end) {
                if (loop && end > start)
                    pos = start; // gapless wrap, mid-block
                else {
                    playing.store(false, std::memory_order_relaxed);
                    break;
                }
            }
            for (uint32_t c = 0; c < channels; ++c) {
                uint32_t src = c < b.channels ? c : b.channels - 1;
                float v = b.samples[pos * b.channels + src];
                out[f * channels + c] = v;
                if (c < 2)
                    pk[c] = std::max(pk[c], std::fabs(v));
            }
            ++pos;
        }
        peaks[0].store(pk[0], std::memory_order_relaxed);
        peaks[1].store(pk[1], std::memory_order_relaxed);
        position.store(pos, std::memory_order_relaxed);
    }
};

Player::Player() : impl(new Impl) {}
Player::~Player() { close(); }

bool Player::open(uint32_t sampleRate, uint32_t channels, const std::string& deviceName)
{
    auto* p = impl.get();
    if (!impl->device.open(
            sampleRate, channels,
            [p](float* out, uint32_t frames, uint32_t ch) { p->render(out, frames, ch); },
            deviceName))
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
    impl->rangeStart.store(0);
    impl->endFrame.store(impl->buffer->frames());
    impl->playing.store(true);
}

void Player::playRange(uint64_t startFrame, uint64_t endFrame)
{
    if (!impl->buffer) return;
    impl->position.store(startFrame);
    impl->rangeStart.store(startFrame);
    impl->endFrame.store(endFrame);
    impl->playing.store(true);
}

void Player::stop() { impl->playing.store(false); }
bool Player::isPlaying() const { return impl->playing.load(); }
void Player::setLooping(bool loop) { impl->looping.store(loop); }
bool Player::isLooping() const { return impl->looping.load(); }
uint64_t Player::positionFrames() const { return impl->position.load(); }
void Player::seek(uint64_t frame) { impl->position.store(frame); }

float Player::outputPeak(uint32_t channel) const
{
    return channel < 2 ? impl->peaks[channel].load(std::memory_order_relaxed) : 0.0f;
}

} // namespace snd::audio
