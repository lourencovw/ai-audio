#include "FFTAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace aiaudio {

namespace {

const double kPi = std::acos(-1.0);

size_t nextPowerOfTwo(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// In-place iterative Cooley-Tukey radix-2 FFT. `data.size()` must be a
// power of two.
void fft(std::vector<std::complex<double>> &data) {
    const size_t n = data.size();
    if (n <= 1) return;

    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * kPi / static_cast<double>(len);
        std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = data[i + k];
                auto v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace

FFTAnalyzer::FFTAnalyzer(uint32_t sampleRate, size_t windowSize)
    : sampleRate_(sampleRate),
      windowSize_(nextPowerOfTwo(windowSize)),
      hopSize_(nextPowerOfTwo(windowSize) / 2) {
    window_.resize(windowSize_);
    for (size_t i = 0; i < windowSize_; ++i) {
        window_[i] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) /
                                                                static_cast<double>(windowSize_ - 1))));
    }
    accumPower_.assign(windowSize_ / 2 + 1, 0.0);
}

void FFTAnalyzer::feed(const float *samples, size_t count) {
    ring_.insert(ring_.end(), samples, samples + count);
    while (ring_.size() >= windowSize_) {
        processWindow();
        ring_.erase(ring_.begin(), ring_.begin() + static_cast<long>(hopSize_));
    }
}

void FFTAnalyzer::processWindow() {
    std::vector<std::complex<double>> buf(windowSize_);
    for (size_t i = 0; i < windowSize_; ++i) {
        buf[i] = std::complex<double>(static_cast<double>(ring_[i]) * window_[i], 0.0);
    }
    fft(buf);

    for (size_t k = 0; k <= windowSize_ / 2; ++k) {
        double mag = std::abs(buf[k]);
        accumPower_[k] += mag * mag;
    }
    ++windowCount_;
}

SpectralStats FFTAnalyzer::finish() const {
    SpectralStats stats;
    if (windowCount_ == 0) return stats;

    double totalEnergy = 0.0;
    double weightedFreqSum = 0.0;
    double lowEnergy = 0.0, midEnergy = 0.0, highEnergy = 0.0;
    double maxPower = -1.0;
    size_t maxBin = 0;

    for (size_t k = 0; k < accumPower_.size(); ++k) {
        double freq = static_cast<double>(k) * sampleRate_ / static_cast<double>(windowSize_);
        double power = accumPower_[k];
        totalEnergy += power;
        weightedFreqSum += freq * power;

        if (freq < 300.0) lowEnergy += power;
        else if (freq < 3400.0) midEnergy += power;
        else highEnergy += power;

        if (power > maxPower) {
            maxPower = power;
            maxBin = k;
        }
    }

    if (totalEnergy > 0.0) {
        stats.spectralCentroidHz = weightedFreqSum / totalEnergy;
        stats.lowBandEnergyFraction = lowEnergy / totalEnergy;
        stats.midBandEnergyFraction = midEnergy / totalEnergy;
        stats.highBandEnergyFraction = highEnergy / totalEnergy;
    }
    stats.dominantFrequencyHz = static_cast<double>(maxBin) * sampleRate_ / static_cast<double>(windowSize_);

    return stats;
}

} // namespace aiaudio
