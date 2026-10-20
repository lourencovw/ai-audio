#pragma once
#include "AudioTypes.h"
#include <cstdint>
#include <vector>

namespace aiaudio {

struct SpectralStats {
    double spectralCentroidHz = 0.0;
    double dominantFrequencyHz = 0.0;
    double lowBandEnergyFraction = 0.0;  // < 300 Hz (rumble, hum, mechanical thuds)
    double midBandEnergyFraction = 0.0;  // 300 Hz - 3.4 kHz (voice band)
    double highBandEnergyFraction = 0.0; // > 3.4 kHz (sibilance, hiss, clicks)
};

// Averages windowed FFT magnitude spectra (Welch's method) over an entire
// calibration stage to produce a stable, low-variance frequency
// fingerprint, rather than an instantaneous/noisy single-frame spectrum.
class FFTAnalyzer {
public:
    explicit FFTAnalyzer(uint32_t sampleRate, size_t windowSize = 1024);

    void feed(const float *samples, size_t count);
    SpectralStats finish() const;

private:
    void processWindow();

    uint32_t sampleRate_;
    size_t windowSize_;
    size_t hopSize_;
    std::vector<float> window_;      // Hann coefficients
    std::vector<float> ring_;        // sliding input buffer
    std::vector<double> accumPower_; // summed power spectrum, bins [0..windowSize_/2]
    size_t windowCount_ = 0;
};

} // namespace aiaudio
