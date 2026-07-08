// snd::audio implementation over miniaudio. The single MINIAUDIO_IMPLEMENTATION
// for the whole library lives here.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "snd/audio.h"

#include <FLAC/stream_encoder.h>

// runtime library loading, portable: dlopen on POSIX, LoadLibrary on Windows
#if defined(_WIN32)
#include <windows.h>
#define RTLD_LAZY 0
#define RTLD_LOCAL 0
namespace {
void* dlopen(const char* name, int) { return (void*)LoadLibraryA(name); }
void* dlsym(void* lib, const char* sym)
{
    return (void*)GetProcAddress((HMODULE)lib, sym);
}
} // namespace
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <vector>

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

    // Preallocate from the reported length and decode straight into place --
    // repeated append/realloc was the slow path on long files.
    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    if (totalFrames > 0) {
        out.samples.resize((size_t)totalFrames * out.channels);
        ma_uint64 done = 0;
        while (done < totalFrames) {
            ma_uint64 want = std::min<ma_uint64>(totalFrames - done, 1 << 16);
            ma_uint64 read = 0;
            ma_result r = ma_decoder_read_pcm_frames(
                &decoder, out.samples.data() + done * out.channels, want, &read);
            done += read;
            if (r != MA_SUCCESS || read < want)
                break;
        }
        out.samples.resize((size_t)done * out.channels);
    } else {
        // Length unknown (some streams): fall back to chunked append.
        float chunk[4096];
        const ma_uint64 chunkFrames = 4096 / decoder.outputChannels;
        for (;;) {
            ma_uint64 read = 0;
            ma_result r = ma_decoder_read_pcm_frames(&decoder, chunk, chunkFrames, &read);
            if (read > 0)
                out.samples.insert(out.samples.end(), chunk,
                                   chunk + read * decoder.outputChannels);
            if (r != MA_SUCCESS || read < chunkFrames)
                break;
        }
    }

    ma_decoder_uninit(&decoder);
    if (out.samples.empty()) {
        if (error) *error = "decoded zero frames: " + path;
        return false;
    }
    return true;
}

bool probe(const std::string& path, uint32_t& channels, uint32_t& sampleRate,
           uint64_t& frames)
{
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS)
        return false;
    channels = decoder.outputChannels;
    sampleRate = decoder.outputSampleRate;
    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total);
    frames = total;
    ma_decoder_uninit(&decoder);
    return true;
}

bool loadPrefix(const std::string& path, Buffer& out, uint64_t maxFrames, std::string* error)
{
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS) {
        if (error) *error = "could not open/decode: " + path;
        return false;
    }
    out.channels = decoder.outputChannels;
    out.sampleRate = decoder.outputSampleRate;
    out.samples.assign((size_t)maxFrames * out.channels, 0.0f);
    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&decoder, out.samples.data(), maxFrames, &read);
    out.samples.resize((size_t)read * out.channels);
    ma_decoder_uninit(&decoder);
    return read > 0;
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

bool saveFlac(const std::string& path, const Buffer& buf, std::string* error)
{
    if (buf.channels == 0 || buf.sampleRate == 0 || buf.samples.empty()) {
        if (error) *error = "empty buffer";
        return false;
    }
    if (buf.channels > 8) {
        if (error) *error = "FLAC supports at most 8 channels";
        return false;
    }

    FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
    if (!enc) {
        if (error) *error = "encoder alloc failed";
        return false;
    }
    FLAC__stream_encoder_set_channels(enc, buf.channels);
    FLAC__stream_encoder_set_bits_per_sample(enc, 24);
    FLAC__stream_encoder_set_sample_rate(enc, buf.sampleRate);
    FLAC__stream_encoder_set_compression_level(enc, 5);

    // Write through our own std::ofstream rather than libFLAC's init_file:
    // portable, and it avoids libFLAC's own Windows file layer.
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "could not open for writing: " + path;
        FLAC__stream_encoder_delete(enc);
        return false;
    }
    auto writeCb = [](const FLAC__StreamEncoder*, const FLAC__byte buffer[],
                      size_t bytes, uint32_t, uint32_t, void* cd)
        -> FLAC__StreamEncoderWriteStatus {
        auto* os = static_cast<std::ofstream*>(cd);
        os->write(reinterpret_cast<const char*>(buffer), (std::streamsize)bytes);
        return os->good() ? FLAC__STREAM_ENCODER_WRITE_STATUS_OK
                          : FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
    };
    auto seekCb = [](const FLAC__StreamEncoder*, FLAC__uint64 off, void* cd)
        -> FLAC__StreamEncoderSeekStatus {
        auto* os = static_cast<std::ofstream*>(cd);
        os->seekp((std::streamoff)off);
        return os->good() ? FLAC__STREAM_ENCODER_SEEK_STATUS_OK
                          : FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
    };
    auto tellCb = [](const FLAC__StreamEncoder*, FLAC__uint64* off, void* cd)
        -> FLAC__StreamEncoderTellStatus {
        auto* os = static_cast<std::ofstream*>(cd);
        auto p = os->tellp();
        if (p < 0)
            return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
        *off = (FLAC__uint64)p;
        return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
    };
    if (FLAC__stream_encoder_init_stream(enc, writeCb, seekCb, tellCb, nullptr,
                                         &out) !=
        FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        if (error) *error = "FLAC encoder init failed: " + path;
        FLAC__stream_encoder_delete(enc);
        return false;
    }

    // float -> 24-bit, in chunks
    const uint64_t total = buf.frames();
    const uint32_t chunk = 4096;
    std::vector<FLAC__int32> tmp((size_t)chunk * buf.channels);
    bool ok = true;
    for (uint64_t pos = 0; pos < total && ok; pos += chunk) {
        uint32_t n = (uint32_t)std::min<uint64_t>(chunk, total - pos);
        for (uint32_t i = 0; i < n * buf.channels; ++i) {
            float s = buf.samples[pos * buf.channels + i];
            s = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
            tmp[i] = (FLAC__int32)std::lrintf(s * 8388607.0f);
        }
        ok = FLAC__stream_encoder_process_interleaved(enc, tmp.data(), n);
    }
    ok = FLAC__stream_encoder_finish(enc) && ok;
    FLAC__stream_encoder_delete(enc);
    if (!ok && error)
        *error = "FLAC encode failed: " + path;
    return ok;
}

// --- MP3 via libmp3lame, loaded at runtime (LGPL kept out of our binary) ---

namespace {

struct LameApi {
    void* lib = nullptr;
    void* (*init)() = nullptr;
    int (*set_in_samplerate)(void*, int) = nullptr;
    int (*set_num_channels)(void*, int) = nullptr;
    int (*set_brate)(void*, int) = nullptr;
    int (*set_quality)(void*, int) = nullptr;
    int (*init_params)(void*) = nullptr;
    int (*encode_interleaved_float)(void*, const float*, int, unsigned char*,
                                    int) = nullptr;
    int (*encode_float)(void*, const float*, const float*, int, unsigned char*,
                        int) = nullptr;
    int (*encode_flush)(void*, unsigned char*, int) = nullptr;
    int (*close)(void*) = nullptr;

    bool ok() const
    {
        return lib && init && set_in_samplerate && set_num_channels && set_brate &&
               set_quality && init_params && encode_interleaved_float &&
               encode_float && encode_flush && close;
    }
};

const LameApi& lame()
{
    static LameApi api = [] {
        LameApi a;
        const char* candidates[] = {"libmp3lame.dylib",
                                    "/opt/homebrew/lib/libmp3lame.dylib",
                                    "/usr/local/lib/libmp3lame.dylib",
                                    "libmp3lame.so.0", "libmp3lame.so",
                                    "libmp3lame.dll", "lame_enc.dll"};
        for (const char* c : candidates) {
            a.lib = dlopen(c, RTLD_LAZY | RTLD_LOCAL);
            if (a.lib)
                break;
        }
        if (!a.lib)
            return a;
        a.init = (void* (*)())dlsym(a.lib, "lame_init");
        a.set_in_samplerate = (int (*)(void*, int))dlsym(a.lib, "lame_set_in_samplerate");
        a.set_num_channels = (int (*)(void*, int))dlsym(a.lib, "lame_set_num_channels");
        a.set_brate = (int (*)(void*, int))dlsym(a.lib, "lame_set_brate");
        a.set_quality = (int (*)(void*, int))dlsym(a.lib, "lame_set_quality");
        a.init_params = (int (*)(void*))dlsym(a.lib, "lame_init_params");
        a.encode_interleaved_float = (int (*)(void*, const float*, int, unsigned char*,
                                              int))dlsym(a.lib,
                                                         "lame_encode_buffer_interleaved_ieee_float");
        a.encode_float = (int (*)(void*, const float*, const float*, int,
                                  unsigned char*, int))dlsym(a.lib,
                                                             "lame_encode_buffer_ieee_float");
        a.encode_flush = (int (*)(void*, unsigned char*, int))dlsym(a.lib,
                                                                    "lame_encode_flush");
        a.close = (int (*)(void*))dlsym(a.lib, "lame_close");
        return a;
    }();
    return api;
}

} // namespace

bool mp3EncoderAvailable() { return lame().ok(); }

bool saveMp3(const std::string& path, const Buffer& buf, std::string* error)
{
    if (buf.channels == 0 || buf.sampleRate == 0 || buf.samples.empty()) {
        if (error) *error = "empty buffer";
        return false;
    }
    if (buf.channels > 2) {
        if (error) *error = "MP3 is mono/stereo only -- export multichannel as WAV or FLAC";
        return false;
    }
    const LameApi& L = lame();
    if (!L.ok()) {
        if (error)
            *error = "libmp3lame not installed (brew install lame) -- MP3 export "
                     "loads it at runtime";
        return false;
    }

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        if (error) *error = "could not open for writing: " + path;
        return false;
    }

    void* gf = L.init();
    L.set_in_samplerate(gf, (int)buf.sampleRate);
    L.set_num_channels(gf, (int)buf.channels);
    L.set_brate(gf, 256);
    L.set_quality(gf, 2);
    if (L.init_params(gf) < 0) {
        if (error) *error = "LAME rejected the format (rate " +
                            std::to_string(buf.sampleRate) + ")";
        L.close(gf);
        fclose(f);
        return false;
    }

    const uint64_t total = buf.frames();
    const uint32_t chunk = 4096;
    std::vector<unsigned char> mp3(chunk * 5 / 4 + 7200);
    bool ok = true;
    for (uint64_t pos = 0; pos < total && ok; pos += chunk) {
        int n = (int)std::min<uint64_t>(chunk, total - pos);
        int bytes;
        if (buf.channels == 2)
            bytes = L.encode_interleaved_float(gf, buf.samples.data() + pos * 2, n,
                                               mp3.data(), (int)mp3.size());
        else
            bytes = L.encode_float(gf, buf.samples.data() + pos,
                                   buf.samples.data() + pos, n, mp3.data(),
                                   (int)mp3.size());
        ok = bytes >= 0 && (bytes == 0 || fwrite(mp3.data(), 1, bytes, f) == (size_t)bytes);
    }
    if (ok) {
        int bytes = L.encode_flush(gf, mp3.data(), (int)mp3.size());
        ok = bytes >= 0 && (bytes == 0 || fwrite(mp3.data(), 1, bytes, f) == (size_t)bytes);
    }
    L.close(gf);
    fclose(f);
    if (!ok && error)
        *error = "MP3 encode failed: " + path;
    return ok;
}

#if !defined(__APPLE__) && !defined(_WIN32)
// Linux: pipe through ffprobe/ffmpeg when they're on PATH -- nothing linked,
// nothing vendored, reads every container ffmpeg knows.
namespace {

std::string shellQuote(const std::string& s)
{
    std::string out = "'";
    for (char c : s)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    out += "'";
    return out;
}

} // namespace

bool loadMediaAudio(const std::string& path, Buffer& out, std::string* error)
{
    // probe the first audio stream's layout
    std::string probe = "ffprobe -v error -select_streams a:0 -show_entries "
                        "stream=sample_rate,channels -of default=nw=1 " +
                        shellQuote(path) + " 2>/dev/null";
    FILE* pf = popen(probe.c_str(), "r");
    if (!pf) {
        if (error) *error = "could not run ffprobe";
        return false;
    }
    uint32_t channels = 0, rate = 0;
    char line[128];
    while (fgets(line, sizeof(line), pf)) {
        unsigned v = 0;
        if (sscanf(line, "sample_rate=%u", &v) == 1)
            rate = v;
        else if (sscanf(line, "channels=%u", &v) == 1)
            channels = v;
    }
    if (pclose(pf) != 0 || channels == 0 || rate == 0) {
        if (error)
            *error = "no audio track (or ffprobe missing): " + path;
        return false;
    }

    std::string cmd = "ffmpeg -v error -i " + shellQuote(path) +
                      " -map a:0 -f f32le -acodec pcm_f32le - 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) {
        if (error) *error = "could not run ffmpeg";
        return false;
    }
    out.channels = channels;
    out.sampleRate = rate;
    out.samples.clear();
    float chunk[8192];
    size_t got;
    while ((got = fread(chunk, sizeof(float), 8192, f)) > 0)
        out.samples.insert(out.samples.end(), chunk, chunk + got);
    int rc = pclose(f);
    out.samples.resize(out.samples.size() - out.samples.size() % channels);
    if (rc != 0 || out.samples.empty()) {
        if (error) *error = "ffmpeg decode failed: " + path;
        return false;
    }
    return true;
}
#elif defined(_WIN32)
bool loadMediaAudio(const std::string&, Buffer&, std::string* error)
{
    if (error) *error = "media extraction: Media Foundation backend pending";
    return false;
}
#endif

// --- StreamReader -------------------------------------------------------------

struct StreamReader::Impl {
    ma_decoder decoder{};
    bool open = false;
    uint64_t totalFrames = 0;
};

StreamReader::StreamReader() : impl(new Impl) {}
StreamReader::~StreamReader() { close(); }

bool StreamReader::open(const std::string& path, std::string* error)
{
    close();
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_file(path.c_str(), &cfg, &impl->decoder) != MA_SUCCESS) {
        if (error) *error = "could not open: " + path;
        return false;
    }
    ma_uint64 n = 0;
    ma_decoder_get_length_in_pcm_frames(&impl->decoder, &n);
    impl->totalFrames = n;
    impl->open = true;
    return true;
}

void StreamReader::close()
{
    if (impl->open) {
        ma_decoder_uninit(&impl->decoder);
        impl->open = false;
        impl->totalFrames = 0;
    }
}

bool StreamReader::isOpen() const { return impl->open; }
uint32_t StreamReader::channels() const
{
    return impl->open ? impl->decoder.outputChannels : 0;
}
uint32_t StreamReader::sampleRate() const
{
    return impl->open ? impl->decoder.outputSampleRate : 0;
}
uint64_t StreamReader::frames() const { return impl->totalFrames; }

bool StreamReader::seek(uint64_t frame)
{
    return impl->open &&
           ma_decoder_seek_to_pcm_frame(&impl->decoder, frame) == MA_SUCCESS;
}

uint64_t StreamReader::read(float* interleaved, uint64_t maxFrames)
{
    if (!impl->open || !interleaved || maxFrames == 0)
        return 0;
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(&impl->decoder, interleaved, maxFrames, &got);
    return got;
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

    InsertHook hook;
    std::atomic_flag hookLock = ATOMIC_FLAG_INIT;

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

        for (uint32_t f = 0; f < frames; ++f) {
            if (pos >= end) {
                if (loop && end > start)
                    pos = start; // gapless wrap, mid-block
                else {
                    playing.store(false, std::memory_order_relaxed);
                    break;
                }
            }
            if (b.channels <= channels) {
                for (uint32_t c = 0; c < channels; ++c) {
                    uint32_t src = c < b.channels ? c : b.channels - 1;
                    out[f * channels + c] = b.samples[pos * b.channels + src];
                }
            } else {
                // fold-down (5.1 etc.): extras distribute alternately at -3 dB
                for (uint32_t c = 0; c < channels; ++c)
                    out[f * channels + c] = b.samples[pos * b.channels + c];
                for (uint32_t c = channels; c < b.channels; ++c)
                    out[f * channels + (c % channels)] +=
                        0.707f * b.samples[pos * b.channels + c];
            }
            ++pos;
        }
        position.store(pos, std::memory_order_relaxed);

        // live insert chain: try-lock so a main-thread hook swap can never
        // block audio -- worst case one raw block slips through
        if (!hookLock.test_and_set(std::memory_order_acquire)) {
            if (hook)
                hook(out, frames, channels);
            hookLock.clear(std::memory_order_release);
        }

        // meters read POST-insert levels
        float pk[2] = {0.0f, 0.0f};
        for (uint32_t f = 0; f < frames; ++f)
            for (uint32_t c = 0; c < std::min(channels, 2u); ++c)
                pk[c] = std::max(pk[c], std::fabs(out[f * channels + c]));
        peaks[0].store(pk[0], std::memory_order_relaxed);
        peaks[1].store(pk[1], std::memory_order_relaxed);
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

void Player::setRange(uint64_t startFrame, uint64_t endFrame)
{
    impl->rangeStart.store(startFrame);
    impl->endFrame.store(endFrame);
}

void Player::setInsert(InsertHook hook)
{
    while (impl->hookLock.test_and_set(std::memory_order_acquire)) {} // brief
    impl->hook = std::move(hook);
    impl->hookLock.clear(std::memory_order_release);
}
uint64_t Player::positionFrames() const { return impl->position.load(); }
void Player::seek(uint64_t frame) { impl->position.store(frame); }

float Player::outputPeak(uint32_t channel) const
{
    return channel < 2 ? impl->peaks[channel].load(std::memory_order_relaxed) : 0.0f;
}

} // namespace snd::audio
