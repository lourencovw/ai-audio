#include "minitest.h"
#include "CalibrationSession.h"
#include <algorithm>
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

std::vector<float> makeClick(size_t n, double amplitude, double sampleRate) {
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        double envelope = std::exp(-8.0 * static_cast<double>(i) / static_cast<double>(n));
        out[i] = static_cast<float>(amplitude * envelope *
                                     std::sin(2.0 * kPi * 2000.0 * static_cast<double>(i) / sampleRate));
    }
    return out;
}

std::vector<float> makeThreeClaps(double sampleRate) {
    std::vector<float> signal;
    auto gap = makeSilence(static_cast<size_t>(0.3 * sampleRate));
    for (int i = 0; i < 3; ++i) {
        signal.insert(signal.end(), gap.begin(), gap.end());
        auto click = makeClick(static_cast<size_t>(0.02 * sampleRate), 0.9, sampleRate);
        signal.insert(signal.end(), click.begin(), click.end());
    }
    signal.insert(signal.end(), gap.begin(), gap.end());
    return signal;
}

// Feeds a signal in small chunks to mimic how a real-time audio callback
// (e.g. OBS's filter_audio) would deliver samples a few hundred frames at
// a time, rather than one giant buffer.
void feedInChunks(CalibrationSession &session, const std::vector<float> &signal, size_t chunkSize = 480) {
    size_t i = 0;
    while (i < signal.size()) {
        size_t n = std::min(chunkSize, signal.size() - i);
        session.feed(signal.data() + i, n);
        i += n;
    }
}

} // namespace

TEST_CASE(SessionStartsOnNormalSpeechStage) {
    CalibrationSession session(48000);
    CHECK(session.currentStage() == CalibrationStage::NormalSpeech);
    CHECK(!session.isComplete());
    CHECK(session.isWaitingToStart());
    CHECK(!session.isStageActive());
}

TEST_CASE(FeedIsIgnoredUntilStageIsExplicitlyStarted) {
    const double sampleRate = 48000.0;
    CalibrationSession session(static_cast<uint32_t>(sampleRate));
    auto speech = makeSine(static_cast<size_t>(10.0 * sampleRate), 150.0, sampleRate, 0.5);

    // Feeding a full stage's worth of audio with no startCurrentStage()
    // call must not advance anything -- the "user pressed Start" gate.
    feedInChunks(session, speech);
    CHECK(session.currentStage() == CalibrationStage::NormalSpeech);
    CHECK(session.isWaitingToStart());
    CHECK_NEAR(session.currentStageElapsedSeconds(), 0.0, 0.001);

    session.startCurrentStage();
    CHECK(session.isStageActive());
    feedInChunks(session, speech);
    CHECK(session.currentStage() == CalibrationStage::LoudSpeech); // advanced after starting
    CHECK(session.isWaitingToStart()); // new stage again awaits its own Start
}

TEST_CASE(RestartingAnActiveStageResetsItsProgress) {
    const double sampleRate = 48000.0;
    CalibrationSession session(static_cast<uint32_t>(sampleRate));
    auto halfStage = makeSine(static_cast<size_t>(5.0 * sampleRate), 150.0, sampleRate, 0.5);

    session.startCurrentStage();
    feedInChunks(session, halfStage);
    CHECK(session.currentStageElapsedSeconds() > 4.0);

    session.startCurrentStage(); // user clicks Start again mid-stage to redo it
    CHECK_NEAR(session.currentStageElapsedSeconds(), 0.0, 0.001);
    CHECK(session.currentStage() == CalibrationStage::NormalSpeech); // still the same stage
    CHECK(session.isStageActive());
}

TEST_CASE(LiveClapCountIncreasesDuringClappingStage) {
    const double sampleRate = 48000.0;
    CalibrationSession session(static_cast<uint32_t>(sampleRate));

    // Fast-forward to the Clapping stage.
    auto speech = makeSine(static_cast<size_t>(10.0 * sampleRate), 150.0, sampleRate, 0.5);
    auto silence = makeSilence(static_cast<size_t>(10.0 * sampleRate));
    for (int i = 0; i < 3; ++i) {
        session.startCurrentStage();
        feedInChunks(session, speech);
    }
    session.startCurrentStage();
    feedInChunks(session, silence);
    CHECK(session.currentStage() == CalibrationStage::Clapping);

    session.startCurrentStage();
    CHECK(session.currentStageTransientCount() == 0);

    auto gap = makeSilence(static_cast<size_t>(0.3 * sampleRate));
    auto click = makeClick(static_cast<size_t>(0.02 * sampleRate), 0.9, sampleRate);

    feedInChunks(session, gap);
    feedInChunks(session, click);
    CHECK(session.currentStageTransientCount() == 1);

    feedInChunks(session, gap);
    feedInChunks(session, click);
    CHECK(session.currentStageTransientCount() == 2);

    feedInChunks(session, gap);
    feedInChunks(session, click);
    // The 3rd clap should both be reflected live and finish the stage.
    CHECK(session.currentStage() == CalibrationStage::Keyboard);
    const auto *clapResult = session.profile().findStage(CalibrationStage::Clapping);
    CHECK(clapResult != nullptr);
    CHECK(clapResult->transientCount == 3);
}

TEST_CASE(SessionAdvancesThroughAllEightStagesInOrder) {
    const double sampleRate = 48000.0;
    CalibrationSession session(static_cast<uint32_t>(sampleRate));

    std::vector<CalibrationStage> seenOrder;
    session.setStageChangedCallback([&](CalibrationStage s) { seenOrder.push_back(s); });

    auto speech = makeSine(static_cast<size_t>(10.5 * sampleRate), 150.0, sampleRate, 0.5);
    auto silence = makeSilence(static_cast<size_t>(10.5 * sampleRate));
    auto claps = makeThreeClaps(sampleRate);

    session.startCurrentStage();
    feedInChunks(session, speech); // NormalSpeech
    session.startCurrentStage();
    feedInChunks(session, speech); // LoudSpeech
    session.startCurrentStage();
    feedInChunks(session, speech); // QuietSpeech
    session.startCurrentStage();
    feedInChunks(session, silence); // Silence
    session.startCurrentStage();
    feedInChunks(session, claps); // Clapping
    session.startCurrentStage();
    feedInChunks(session, speech); // Keyboard (stand-in signal)
    session.startCurrentStage();
    feedInChunks(session, speech); // Mouse
    session.startCurrentStage();
    feedInChunks(session, speech); // Breathing

    CHECK(session.isComplete());
    CHECK(seenOrder.size() == 8);

    std::vector<CalibrationStage> expected = {
        CalibrationStage::LoudSpeech, CalibrationStage::QuietSpeech, CalibrationStage::Silence,
        CalibrationStage::Clapping,   CalibrationStage::Keyboard,    CalibrationStage::Mouse,
        CalibrationStage::Breathing,  CalibrationStage::Count,
    };
    CHECK(seenOrder == expected);
}

TEST_CASE(FinishedProfileHasOneResultPerStageWithExpectedMetrics) {
    const double sampleRate = 48000.0;
    CalibrationSession session(static_cast<uint32_t>(sampleRate));

    // Use exactly-stage-length buffers here (unlike the ordering test above)
    // so nothing bleeds across stage boundaries, keeping the per-stage
    // metric assertions below (e.g. the Silence noise floor) precise.
    auto speech10s = makeSine(static_cast<size_t>(10.0 * sampleRate), 150.0, sampleRate, 0.5);
    auto speech8s = makeSine(static_cast<size_t>(8.0 * sampleRate), 150.0, sampleRate, 0.5);
    auto silence10s = makeSilence(static_cast<size_t>(10.0 * sampleRate));
    auto claps = makeThreeClaps(sampleRate);

    session.startCurrentStage();
    feedInChunks(session, speech10s); // NormalSpeech
    session.startCurrentStage();
    feedInChunks(session, speech10s); // LoudSpeech
    session.startCurrentStage();
    feedInChunks(session, speech10s); // QuietSpeech
    session.startCurrentStage();
    feedInChunks(session, silence10s); // Silence
    session.startCurrentStage();
    feedInChunks(session, claps); // Clapping
    session.startCurrentStage();
    feedInChunks(session, speech10s); // Keyboard (stand-in signal)
    session.startCurrentStage();
    feedInChunks(session, speech8s); // Mouse
    session.startCurrentStage();
    feedInChunks(session, speech8s); // Breathing

    CHECK(session.isComplete());
    const auto &profile = session.profile();
    CHECK(profile.stageResults().size() == 8);

    const auto *silenceResult = profile.findStage(CalibrationStage::Silence);
    CHECK(silenceResult != nullptr);
    CHECK(silenceResult->level.rmsDbfsAvg <= kSilenceFloorDbfs + 0.01);

    const auto *clapResult = profile.findStage(CalibrationStage::Clapping);
    CHECK(clapResult != nullptr);
    CHECK(clapResult->transientCount == 3);

    std::string json = profile.toJson();
    CHECK(json.find("\"stage\": \"Silence\"") != std::string::npos);
    CHECK(json.find("\"transientCount\": 3") != std::string::npos);
}
