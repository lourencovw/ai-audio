#include "minitest.h"
#include "TransientDetector.h"
#include <cmath>
#include <vector>

using namespace aiaudio;

namespace {

const double kPi = std::acos(-1.0);

std::vector<float> makeSilence(size_t n) { return std::vector<float>(n, 0.0f); }

// A short, loud, fast-decaying burst meant to approximate a clap/keystroke/
// mouse-click transient: sharp attack followed by a quick decay back to
// near-silence, unlike sustained speech or tones.
std::vector<float> makeClick(size_t n, double amplitude, double sampleRate) {
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        double envelope = std::exp(-8.0 * static_cast<double>(i) / static_cast<double>(n));
        out[i] = static_cast<float>(amplitude * envelope *
                                     std::sin(2.0 * kPi * 2000.0 * static_cast<double>(i) / sampleRate));
    }
    return out;
}

} // namespace

TEST_CASE(SilenceProducesNoTransients) {
    auto silence = makeSilence(48000);
    TransientDetector detector(48000);
    detector.feed(silence.data(), silence.size());
    CHECK(detector.transientCount() == 0);
}

TEST_CASE(ThreeClapsAreCountedAsThreeTransients) {
    const double sampleRate = 48000.0;
    std::vector<float> signal;
    auto gap = makeSilence(static_cast<size_t>(0.3 * sampleRate));
    for (int i = 0; i < 3; ++i) {
        signal.insert(signal.end(), gap.begin(), gap.end());
        auto click = makeClick(static_cast<size_t>(0.02 * sampleRate), 0.9, sampleRate);
        signal.insert(signal.end(), click.begin(), click.end());
    }
    signal.insert(signal.end(), gap.begin(), gap.end());

    TransientDetector detector(static_cast<uint32_t>(sampleRate));
    detector.feed(signal.data(), signal.size());
    CHECK(detector.transientCount() == 3);
}

TEST_CASE(ContinuousLoudToneCountsAsOneTransientNotMany) {
    const double sampleRate = 48000.0;
    auto silence = makeSilence(4800);
    std::vector<float> sustained(48000);
    for (size_t i = 0; i < sustained.size(); ++i)
        sustained[i] = static_cast<float>(0.9 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / sampleRate));

    std::vector<float> signal;
    signal.insert(signal.end(), silence.begin(), silence.end());
    signal.insert(signal.end(), sustained.begin(), sustained.end());

    TransientDetector detector(static_cast<uint32_t>(sampleRate));
    detector.feed(signal.data(), signal.size());
    CHECK(detector.transientCount() == 1);
}

TEST_CASE(RapidKeystrokesAreCountedSeparately) {
    const double sampleRate = 48000.0;
    std::vector<float> signal;
    auto gap = makeSilence(static_cast<size_t>(0.1 * sampleRate)); // short gaps, like fast typing
    for (int i = 0; i < 6; ++i) {
        signal.insert(signal.end(), gap.begin(), gap.end());
        auto click = makeClick(static_cast<size_t>(0.01 * sampleRate), 0.6, sampleRate);
        signal.insert(signal.end(), click.begin(), click.end());
    }

    TransientDetector detector(static_cast<uint32_t>(sampleRate));
    detector.feed(signal.data(), signal.size());
    CHECK(detector.transientCount() == 6);
}
