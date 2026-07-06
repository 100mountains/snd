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

// Write a 32-bit float WAV.
bool saveWav(const std::string& path, const Buffer& buf, std::string* error = nullptr);

// Playback device. The callback runs on the audio thread: no allocation, no
// locks, no I/O in it.
class Device {
public:
    using Callback = std::function<void(float* output, uint32_t frames, uint32_t channels)>;

    Device();
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    bool open(uint32_t sampleRate = 48000, uint32_t channels = 2, Callback cb = {});
    void close();
    bool start();
    void stop();
    bool isOpen() const;

    std::string name() const;
    uint32_t sampleRate() const;
    uint32_t channels() const;

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

    bool open(uint32_t sampleRate = 48000, uint32_t channels = 1, Callback cb = {});
    void close();
    bool start();
    void stop();

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

    bool open(uint32_t sampleRate = 48000, uint32_t channels = 2);
    void close();

    // The buffer must outlive playback. Sample-rate conversion is not done;
    // open the player at the buffer's rate for correct pitch.
    void setBuffer(const Buffer* buf);

    void play();
    void playRange(uint64_t startFrame, uint64_t endFrame); // half-open [start,end)
    void stop();
    bool isPlaying() const;

    uint64_t positionFrames() const;
    void seek(uint64_t frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace snd::audio
