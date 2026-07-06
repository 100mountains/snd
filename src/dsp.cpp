// snd::dsp implementation over PFFFT.
//
// PFFFT's ordered real-FFT layout: output[0] = DC, output[1] = Nyquist,
// then interleaved re/im pairs for bins 1..N/2-1. We unpack that into a
// plain DC..Nyquist complex array so callers get the textbook layout.

#include "snd/dsp.h"

#include <pffft.h>

#include <cassert>
#include <cmath>
#include <cstring>

namespace snd::dsp {

// ---------------------------------------------------------------------------
// RealFFT
// ---------------------------------------------------------------------------

struct RealFFT::Impl {
    PFFFT_Setup* setup = nullptr;
    uint32_t n = 0;
    float* work = nullptr;    // pffft-aligned scratch
    float* ordered = nullptr; // pffft-aligned packed spectrum
};

RealFFT::RealFFT(uint32_t size) : impl(new Impl)
{
    assert(size >= 32 && (size & (size - 1)) == 0);
    impl->n = size;
    impl->setup = pffft_new_setup((int)size, PFFFT_REAL);
    impl->work = (float*)pffft_aligned_malloc(size * sizeof(float));
    impl->ordered = (float*)pffft_aligned_malloc(size * sizeof(float));
}

RealFFT::~RealFFT()
{
    if (impl->setup)
        pffft_destroy_setup(impl->setup);
    pffft_aligned_free(impl->work);
    pffft_aligned_free(impl->ordered);
}

uint32_t RealFFT::size() const { return impl->n; }
uint32_t RealFFT::bins() const { return impl->n / 2 + 1; }

void RealFFT::forward(const float* in, std::complex<float>* out)
{
    const uint32_t n = impl->n;
    pffft_transform_ordered(impl->setup, in, impl->ordered, impl->work, PFFFT_FORWARD);

    out[0] = {impl->ordered[0], 0.0f};      // DC
    out[n / 2] = {impl->ordered[1], 0.0f};  // Nyquist
    for (uint32_t k = 1; k < n / 2; ++k)
        out[k] = {impl->ordered[2 * k], impl->ordered[2 * k + 1]};
}

void RealFFT::inverse(const std::complex<float>* in, float* out)
{
    const uint32_t n = impl->n;
    impl->ordered[0] = in[0].real();
    impl->ordered[1] = in[n / 2].real();
    for (uint32_t k = 1; k < n / 2; ++k) {
        impl->ordered[2 * k] = in[k].real();
        impl->ordered[2 * k + 1] = in[k].imag();
    }

    pffft_transform_ordered(impl->setup, impl->ordered, out, impl->work, PFFFT_BACKWARD);

    const float scale = 1.0f / (float)n; // pffft's inverse is unnormalized
    for (uint32_t i = 0; i < n; ++i)
        out[i] *= scale;
}

// ---------------------------------------------------------------------------
// STFT (Hann analysis + Hann synthesis, hop = fftSize/4)
//
// With w(n) = Hann and 75% overlap, sum_k w^2(n - kH) = 3/2 exactly, so
// resynthesis divides by that constant: analyze->resynth is transparent.
// ---------------------------------------------------------------------------

static std::vector<float> hannWindow(uint32_t n)
{
    std::vector<float> w(n);
    for (uint32_t i = 0; i < n; ++i)
        w[i] = 0.5f - 0.5f * std::cos(2.0 * M_PI * i / (double)n);
    return w;
}

StftData stftAnalyze(const float* samples, uint64_t length, const StftConfig& config)
{
    StftData data;
    data.config = config;
    data.originalLength = length;

    const uint32_t N = config.fftSize;
    const uint32_t H = config.hop;
    data.bins = N / 2 + 1;

    // Frames cover [ -N+H, length ) starting positions so edges reconstruct.
    // Simpler and standard: pad the signal by N on both sides conceptually.
    const uint64_t padded = length + N;
    data.numFrames = (uint32_t)((padded + H - 1) / H) + 1;

    RealFFT fft(N);
    auto window = hannWindow(N);
    std::vector<float> frame(N);
    data.spectra.resize((size_t)data.numFrames * data.bins);

    for (uint32_t f = 0; f < data.numFrames; ++f) {
        const int64_t start = (int64_t)f * H - (int64_t)(N / 2); // centered frames
        for (uint32_t i = 0; i < N; ++i) {
            int64_t idx = start + i;
            float s = (idx >= 0 && idx < (int64_t)length) ? samples[idx] : 0.0f;
            frame[i] = s * window[i];
        }
        fft.forward(frame.data(), data.frame(f));
    }
    return data;
}

std::vector<float> stftResynthesize(const StftData& data)
{
    const uint32_t N = data.config.fftSize;
    const uint32_t H = data.config.hop;

    RealFFT fft(N);
    auto window = hannWindow(N);

    // Output accumulator, padded to cover frame extents; trimmed at the end.
    const int64_t pad = N / 2;
    std::vector<float> acc(data.originalLength + N * 2, 0.0f);
    std::vector<float> frame(N);

    for (uint32_t f = 0; f < data.numFrames; ++f) {
        fft.inverse(data.frame(f), frame.data());
        const int64_t start = (int64_t)f * H - pad;
        for (uint32_t i = 0; i < N; ++i) {
            int64_t idx = start + i + pad; // shift into acc space
            if (idx >= 0 && idx < (int64_t)acc.size())
                acc[idx] += frame[i] * window[i];
        }
    }

    // Hann^2 at 75% overlap sums to 1.5.
    const float norm = 1.0f / 1.5f;
    std::vector<float> out(data.originalLength);
    for (uint64_t i = 0; i < data.originalLength; ++i)
        out[i] = acc[i + pad] * norm;
    return out;
}

} // namespace snd::dsp
