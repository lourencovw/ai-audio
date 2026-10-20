#include "minitest.h"
#include "AudioAnalyzer.h"
#include <cmath>
#include <vector>

using namespace aiaudio;

namespace {

const double kPi = std::acos(-1.0);

std::vector<float> makeSilence(size_t n) { return std::vector<float>(n, 0.0f); }

std::vector<float> makeSine(size_t n, double freq, double sampleRate, double amplitude) {
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * freq * static_cast<double>(i) / sampleRate));
    return out;
}

} // namespace

TEST_CASE(FullScaleSineHasRmsAroundMinus3Dbfs) {
    auto sine = makeSine(48000, 1000.0, 48000.0, 1.0);
    AudioAnalyzer analyzer(48000);
    analyzer.feed(sine.data(), sine.size());
    auto stats = analyzer.finish();
    // RMS of a full-scale sine wave is 1/sqrt(2) => ~-3.01 dBFS.
    CHECK_NEAR(stats.rmsDbfsAvg, -3.01, 0.2);
    CHECK_NEAR(stats.peakDbfs, 0.0, 0.2);
}

TEST_CASE(DigitalSilenceClampsToFloor) {
    auto silence = makeSilence(48000);
    AudioAnalyzer analyzer(48000);
    analyzer.feed(silence.data(), silence.size());
    auto stats = analyzer.finish();
    CHECK(stats.rmsDbfsAvg <= kSilenceFloorDbfs + 0.01);
    CHECK(stats.noiseFloorDbfs <= kSilenceFloorDbfs + 0.01);
}

TEST_CASE(QuietSignalHasMuchLowerRmsThanLoudSignal) {
    auto loud = makeSine(48000, 1000.0, 48000.0, 0.9);
    auto quiet = makeSine(48000, 1000.0, 48000.0, 0.05);

    AudioAnalyzer loudAnalyzer(48000);
    loudAnalyzer.feed(loud.data(), loud.size());
    AudioAnalyzer quietAnalyzer(48000);
    quietAnalyzer.feed(quiet.data(), quiet.size());

    CHECK(loudAnalyzer.finish().rmsDbfsAvg > quietAnalyzer.finish().rmsDbfsAvg + 10.0);
}

TEST_CASE(NoiseFloorReflectsQuietGapsBetweenLoudBursts) {
    // Simulate speech-like bursts separated by near-silence gaps.
    const double sampleRate = 48000.0;
    std::vector<float> signal;
    for (int rep = 0; rep < 10; ++rep) {
        auto burst = makeSine(static_cast<size_t>(0.2 * sampleRate), 200.0, sampleRate, 0.8);
        auto gap = makeSine(static_cast<size_t>(0.2 * sampleRate), 200.0, sampleRate, 0.001);
        signal.insert(signal.end(), burst.begin(), burst.end());
        signal.insert(signal.end(), gap.begin(), gap.end());
    }

    AudioAnalyzer analyzer(static_cast<uint32_t>(sampleRate));
    analyzer.feed(signal.data(), signal.size());
    auto stats = analyzer.finish();

    // The 10th percentile of block RMS should land near the quiet gaps,
    // well below the overall average (dominated by the loud bursts).
    CHECK(stats.noiseFloorDbfs < stats.rmsDbfsAvg - 20.0);
}
