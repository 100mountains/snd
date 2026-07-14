// snd::audio -- device I/O and audio-file decode/encode.
// Wraps miniaudio; consumers never touch ma_* types.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace snd::audio {

// Interleaved float32 audio in memory.
struct Buffer {
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    std::vector<float> samples; // interleaved, frames() * channels

    uint64_t frames() const { return channels ? samples.size() / channels : 0; }
    double seconds() const { return sampleRate ? (double)frames() / sampleRate : 0.0; }
};

// Decode any format miniaudio understands (wav/flac/mp3/ogg...) to float32.
bool load(const std::string& path, Buffer& out, std::string* error = nullptr);

// Header-only probe: channels/rate/length without decoding the audio.
// Much faster than load() for building file lists.
bool probe(const std::string& path, uint32_t& channels, uint32_t& sampleRate,
           uint64_t& frames);

// Decode only the first maxFrames frames (for previews/lead-in analysis).
bool loadPrefix(const std::string& path, Buffer& out, uint64_t maxFrames,
                std::string* error = nullptr);

// Write a 32-bit float WAV.
bool saveWav(const std::string& path, const Buffer& buf, std::string* error = nullptr);

// Write a FLAC (24-bit, up to 8 channels; vendored libFLAC).
bool saveFlac(const std::string& path, const Buffer& buf, std::string* error = nullptr);

// Write an MP3 (mono/stereo only). libmp3lame is loaded AT RUNTIME (keeps
// the LGPL at arm's length); when it isn't installed this fails with a
// message saying so. mp3EncoderAvailable() lets UIs grey the option out.
bool saveMp3(const std::string& path, const Buffer& buf, std::string* error = nullptr);
bool mp3EncoderAvailable();

// Extract the audio track from any media file the OS can read -- video
// (mp4/mov/m4v...) or audio (m4a/aac...). Preserves the track's channel
// count (5.1 film audio arrives as 6 channels) and sample rate.
// macOS uses AVFoundation, Windows uses Media Foundation, and Linux pipes
// through ffprobe/ffmpeg when those tools are on PATH.
bool loadMediaAudio(const std::string& path, Buffer& out, std::string* error = nullptr);

// Resample to a new rate (miniaudio's resampler, highest built-in quality).
bool resample(const Buffer& in, uint32_t newRate, Buffer& out, std::string* error = nullptr);

// Stream frames from disk without loading the whole file -- sampler-style
// feeding of big content. One reader per thread; seek anywhere, read chunks.
class StreamReader {
public:
    StreamReader();
    ~StreamReader();
    StreamReader(const StreamReader&) = delete;
    StreamReader& operator=(const StreamReader&) = delete;

    bool open(const std::string& path, std::string* error = nullptr);
    void close();
    bool isOpen() const;

    uint32_t channels() const;
    uint32_t sampleRate() const;
    uint64_t frames() const;

    bool seek(uint64_t frame);
    // Reads up to maxFrames interleaved frames; returns frames read (0 = end).
    uint64_t read(float* interleaved, uint64_t maxFrames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Incremental float32 WAV writer. Calls perform file I/O and therefore belong
// on a worker/message thread, never in an audio callback.
class StreamWriter {
public:
    enum class WavFormat {
        Float32,
        Pcm24,
    };

    StreamWriter();
    ~StreamWriter();
    StreamWriter(const StreamWriter&) = delete;
    StreamWriter& operator=(const StreamWriter&) = delete;

    bool openWav(const std::string& path, uint32_t channels,
                 uint32_t sampleRate, std::string* error = nullptr);
    bool openWav(const std::string& path, uint32_t channels,
                 uint32_t sampleRate, WavFormat format,
                 std::string* error = nullptr);
    void close();
    bool isOpen() const;
    bool write(const float* interleaved, uint64_t frames,
               std::string* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Names of the available devices, for settings UIs. Order may change between
// calls; select devices by NAME, not index.
std::vector<std::string> playbackDevices();
std::vector<std::string> captureDevices();

// Detailed device information for settings UIs. `channels == 0` means the
// backend reports "any channel count"; `sampleRate == 0` means "any rate".
struct DeviceNativeFormat {
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
};

struct DeviceInfo {
    std::string name;
    bool isDefault = false;
    std::vector<DeviceNativeFormat> nativeFormats;
};

std::vector<DeviceInfo> playbackDeviceInfos();
std::vector<DeviceInfo> captureDeviceInfos();
bool playbackDeviceInfo(const std::string& name, DeviceInfo& out);
bool captureDeviceInfo(const std::string& name, DeviceInfo& out);

// Settings used when opening a device. bufferFrames/bufferMilliseconds are
// hints; backends may choose a nearby period size. `periods == 0` keeps the
// backend default. `activeChannels` is a 0-based enable mask; empty = all.
// Playback devices silence inactive channels after the callback returns.
// Capture devices report the mask for callers/UI; capture callbacks still
// receive the opened channel count.
struct DeviceOptions {
    uint32_t sampleRate = 48000;
    uint32_t channels = 2;
    uint32_t bufferFrames = 0;
    uint32_t bufferMilliseconds = 0;
    uint32_t periods = 0;
    std::string deviceName;
    std::vector<uint32_t> activeChannels;
};

// Playback device. The callback runs on the audio thread: no allocation, no
// locks, no I/O in it.
class Device {
public:
    using Callback = std::function<void(float* output, uint32_t frames, uint32_t channels)>;

    Device();
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // deviceName from playbackDevices(); empty = system default.
    bool open(uint32_t sampleRate = 48000, uint32_t channels = 2, Callback cb = {},
              const std::string& deviceName = {});
    bool open(const DeviceOptions& options, Callback cb = {});
    void close();
    bool start();
    void stop();
    bool isOpen() const;

    std::string name() const;
    uint32_t sampleRate() const;
    uint32_t channels() const;
    uint32_t bufferFrames() const;
    uint32_t periods() const;
    std::vector<uint32_t> activeChannels() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Capture device, same rules.
class CaptureDevice {
public:
    using Callback = std::function<void(const float* input, uint32_t frames, uint32_t channels)>;

    CaptureDevice();
    ~CaptureDevice();
    CaptureDevice(const CaptureDevice&) = delete;
    CaptureDevice& operator=(const CaptureDevice&) = delete;

    // deviceName from captureDevices(); empty = system default.
    bool open(uint32_t sampleRate = 48000, uint32_t channels = 1, Callback cb = {},
              const std::string& deviceName = {});
    bool open(const DeviceOptions& options, Callback cb = {});
    void close();
    bool start();
    void stop();
    bool isOpen() const;
    std::string name() const;
    uint32_t sampleRate() const;
    uint32_t channels() const;
    uint32_t bufferFrames() const;
    uint32_t periods() const;
    std::vector<uint32_t> activeChannels() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// One-buffer player on top of Device: position tracking, play/stop/seek.
// All control from the main thread; the audio thread only reads.
class Player {
public:
    Player();
    ~Player();

    bool open(uint32_t sampleRate = 48000, uint32_t channels = 2,
              const std::string& deviceName = {});
    bool open(const DeviceOptions& options);
    void close();

    // The buffer must outlive playback. Sample-rate conversion is not done;
    // open the player at the buffer's rate for correct pitch.
    void setBuffer(const Buffer* buf);

    void play();
    void playRange(uint64_t startFrame, uint64_t endFrame); // half-open [start,end)
    void stop();
    bool isPlaying() const;

    // Gapless looping of the current range (whole buffer for play(), the
    // given range for playRange()). Takes effect live, from any thread.
    void setLooping(bool loop);
    bool isLooping() const;

    // Retarget the play/loop range without seeking -- lets a loop follow a
    // changing selection live during playback.
    void setRange(uint64_t startFrame, uint64_t endFrame);

    uint64_t positionFrames() const;
    void seek(uint64_t frame);

    // Peak absolute sample level of the most recent audio callback, per
    // channel (0/1), measured AFTER the insert hook. For UI meters.
    float outputPeak(uint32_t channel) const;

    // Insert processor, called on the AUDIO THREAD with the interleaved
    // block after the buffer is rendered (e.g. a live effect chain).
    // The hook must be real-time safe: no allocation, locks, or I/O.
    // Swapping the hook is internally guarded; pass {} to remove.
    using InsertHook = std::function<void(float* interleaved, uint32_t frames,
                                          uint32_t channels)>;
    void setInsert(InsertHook hook);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace snd::audio
