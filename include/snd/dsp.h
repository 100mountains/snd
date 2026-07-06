// snd::dsp -- FFT and STFT for analysis and spectral editing.
// PFFFT underneath; consumers never touch pffft types.
#pragma once

#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace snd::dsp {

// Real-input FFT. size must be a power of two, >= 32.
class RealFFT {
public:
    explicit RealFFT(uint32_t size);
    ~RealFFT();
    RealFFT(const RealFFT&) = delete;
    RealFFT& operator=(const RealFFT&) = delete;

    uint32_t size() const;
    uint32_t bins() const; // size/2 + 1

    // in: size samples. out: bins() complex values (DC..Nyquist).
    void forward(const float* in, std::complex<float>* out);
    // Inverse, normalized (forward then inverse returns the input).
    void inverse(const std::complex<float>* in, float* out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// STFT with Hann analysis + synthesis windows at 75% overlap (COLA-exact):
// analyze -> modify spectra -> resynthesize is transparent when unmodified.
struct StftConfig {
    uint32_t fftSize = 2048; // power of two
    uint32_t hop = 512;      // fftSize/4 for the COLA guarantee
};

struct StftData {
    StftConfig config;
    uint32_t numFrames = 0;
    uint32_t bins = 0;
    uint64_t originalLength = 0;
    std::vector<std::complex<float>> spectra; // numFrames * bins, frame-major

    std::complex<float>* frame(uint32_t f) { return spectra.data() + (size_t)f * bins; }
    const std::complex<float>* frame(uint32_t f) const
    {
        return spectra.data() + (size_t)f * bins;
    }
};

// Analyze one channel of audio.
StftData stftAnalyze(const float* samples, uint64_t length, const StftConfig& config = {});

// Resynthesize back to originalLength samples.
std::vector<float> stftResynthesize(const StftData& data);

// Frequency of a bin at a given sample rate.
inline double binFrequency(uint32_t bin, uint32_t fftSize, double sampleRate)
{
    return (double)bin * sampleRate / (double)fftSize;
}

} // namespace snd::dsp
