#include "minitest.h"
#include "FFTAnalyzer.h"
#include <cmath>
#include <vector>

using namespace aiaudio;

namespace {

const double kPi = std::acos(-1.0);

std::vector<float> makeSine(size_t n, double freq, double sampleRate, double amplitude) {
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * freq * static_cast<double>(i) / sampleRate));
    return out;
}

} // namespace

TEST_CASE(LowFrequencyToneIsClassifiedAsLowBand) {
    auto tone = makeSine(48000 * 2, 100.0, 48000.0, 0.8); // 100 Hz, well below the voice band
    FFTAnalyzer fft(48000);
    fft.feed(tone.data(), tone.size());
    auto stats = fft.finish();

    CHECK(stats.lowBandEnergyFraction > 0.9);
    CHECK_NEAR(stats.dominantFrequencyHz, 100.0, 50.0);
}

TEST_CASE(VoiceBandToneIsClassifiedAsMidBand) {
    auto tone = makeSine(48000 * 2, 1000.0, 48000.0, 0.8); // 1 kHz, squarely in the voice band
    FFTAnalyzer fft(48000);
    fft.feed(tone.data(), tone.size());
    auto stats = fft.finish();

    CHECK(stats.midBandEnergyFraction > 0.9);
    CHECK_NEAR(stats.dominantFrequencyHz, 1000.0, 100.0);
}

TEST_CASE(HighFrequencyToneIsClassifiedAsHighBand) {
    auto tone = makeSine(48000 * 2, 8000.0, 48000.0, 0.8); // 8 kHz, sibilance/hiss range
    FFTAnalyzer fft(48000);
    fft.feed(tone.data(), tone.size());
    auto stats = fft.finish();

    CHECK(stats.highBandEnergyFraction > 0.9);
    CHECK_NEAR(stats.dominantFrequencyHz, 8000.0, 200.0);
}

TEST_CASE(SpectralCentroidTracksDominantFrequency) {
    auto low = makeSine(48000 * 2, 200.0, 48000.0, 0.8);
    auto high = makeSine(48000 * 2, 5000.0, 48000.0, 0.8);

    FFTAnalyzer fftLow(48000);
    fftLow.feed(low.data(), low.size());
    FFTAnalyzer fftHigh(48000);
    fftHigh.feed(high.data(), high.size());

    CHECK(fftHigh.finish().spectralCentroidHz > fftLow.finish().spectralCentroidHz);
}
