#include "minitest.h"
#include "OptimizationEngine.h"
#include <algorithm>

using namespace aiaudio;

namespace {

StageResult makeStage(CalibrationStage stage, double rmsAvg, double peak, double noiseFloor,
                       size_t transientCount = 0) {
    StageResult r;
    r.stage = stage;
    r.level.rmsDbfsAvg = rmsAvg;
    r.level.peakDbfs = peak;
    r.level.noiseFloorDbfs = noiseFloor;
    r.level.rmsDbfsMin = noiseFloor;
    r.level.rmsDbfsMax = peak;
    r.transientCount = transientCount;
    r.capturedDurationSeconds = 10.0;
    return r;
}

// A clean recording: quiet room, good separation between noise floor and
// speech, moderate dynamic range.
AudioProfile makeCleanProfile() {
    AudioProfile profile;
    profile.addStageResult(makeStage(CalibrationStage::NormalSpeech, -20.0, -8.0, -45.0));
    profile.addStageResult(makeStage(CalibrationStage::LoudSpeech, -10.0, -2.0, -40.0));
    profile.addStageResult(makeStage(CalibrationStage::QuietSpeech, -30.0, -18.0, -45.0));
    profile.addStageResult(makeStage(CalibrationStage::Silence, -60.0, -55.0, -58.0));
    profile.addStageResult(makeStage(CalibrationStage::Clapping, -5.0, -1.0, -50.0, 3));
    profile.addStageResult(makeStage(CalibrationStage::Keyboard, -35.0, -20.0, -45.0, 40));
    profile.addStageResult(makeStage(CalibrationStage::Mouse, -38.0, -25.0, -45.0, 6));
    profile.addStageResult(makeStage(CalibrationStage::Breathing, -40.0, -30.0, -45.0));
    return profile;
}

// A noisy recording: the ambient noise floor sits close to the quiet
// speech level (bad room / cheap mic / fan noise), leaving little margin.
AudioProfile makeNoisyProfile() {
    AudioProfile profile;
    profile.addStageResult(makeStage(CalibrationStage::NormalSpeech, -20.0, -8.0, -32.0));
    profile.addStageResult(makeStage(CalibrationStage::LoudSpeech, -10.0, -2.0, -30.0));
    profile.addStageResult(makeStage(CalibrationStage::QuietSpeech, -28.0, -16.0, -32.0));
    profile.addStageResult(makeStage(CalibrationStage::Silence, -30.0, -22.0, -31.0));
    return profile;
}

} // namespace

TEST_CASE(GateOpensAboveNoiseAndBelowQuietSpeechForCleanProfile) {
    auto plan = OptimizationEngine::buildPlan(makeCleanProfile());
    CHECK(plan.gate.openThresholdDb > -58.0); // comfortably above the -58 noise floor
    CHECK(plan.gate.openThresholdDb < -30.0 - 6.0 + 0.01); // comfortably below quiet speech (-30)
    CHECK_NEAR(plan.gate.closeThresholdDb, plan.gate.openThresholdDb - 6.0, 0.01);
}

TEST_CASE(TightNoiseMarginProducesWarningRecommendation) {
    auto plan = OptimizationEngine::buildPlan(makeNoisyProfile());
    bool hasWarning = std::any_of(plan.recommendations.begin(), plan.recommendations.end(), [](const auto &s) {
        return s.find("tight") != std::string::npos;
    });
    CHECK(hasWarning);
}

TEST_CASE(CompressorRatioIsWithinSafeBoundsAndGrowsWithDynamicRange) {
    auto narrowRangePlan = OptimizationEngine::buildPlan(makeNoisyProfile()); // loud(-10) - quiet(-28) = 18 dB
    auto wideRangeProfile = makeCleanProfile();                              // loud(-10) - quiet(-30) = 20 dB
    auto widePlan = OptimizationEngine::buildPlan(wideRangeProfile);

    CHECK(narrowRangePlan.compressor.ratio >= 2.0);
    CHECK(narrowRangePlan.compressor.ratio <= 6.0);
    CHECK(widePlan.compressor.ratio >= 2.0);
    CHECK(widePlan.compressor.ratio <= 6.0);
}

TEST_CASE(GainBoostsQuietMicAndAttenuatesHotMic) {
    AudioProfile quietMic;
    quietMic.addStageResult(makeStage(CalibrationStage::NormalSpeech, -35.0, -20.0, -55.0));
    auto quietPlan = OptimizationEngine::buildPlan(quietMic);
    CHECK(quietPlan.gain.gainDb > 0.0); // target is -18 dBFS, mic reads -35 dBFS => needs boosting

    AudioProfile hotMic;
    hotMic.addStageResult(makeStage(CalibrationStage::NormalSpeech, -5.0, -1.0, -40.0));
    auto hotPlan = OptimizationEngine::buildPlan(hotMic);
    CHECK(hotPlan.gain.gainDb < 0.0); // mic reads -5 dBFS, hotter than target => needs attenuating
}

TEST_CASE(CleanRecordingGetsGentlerNoiseSuppressionThanNoisyRecording) {
    auto cleanPlan = OptimizationEngine::buildPlan(makeCleanProfile());
    auto noisyPlan = OptimizationEngine::buildPlan(makeNoisyProfile());

    // More negative == more aggressive suppression.
    CHECK(cleanPlan.noiseSuppress.suppressLevelDb > noisyPlan.noiseSuppress.suppressLevelDb);
    CHECK(cleanPlan.noiseSuppress.suppressLevelDb >= -30);
    CHECK(cleanPlan.noiseSuppress.suppressLevelDb <= -10);
}

TEST_CASE(KeyboardAndMouseNoiseProduceRecommendationsOnlyWhenDetected) {
    auto plan = OptimizationEngine::buildPlan(makeCleanProfile());
    bool mentionsKeyboard = std::any_of(plan.recommendations.begin(), plan.recommendations.end(),
                                         [](const auto &s) { return s.find("keyboard") != std::string::npos; });
    bool mentionsMouse = std::any_of(plan.recommendations.begin(), plan.recommendations.end(),
                                      [](const auto &s) { return s.find("mouse") != std::string::npos; });
    CHECK(mentionsKeyboard);
    CHECK(mentionsMouse);

    AudioProfile minimal;
    minimal.addStageResult(makeStage(CalibrationStage::NormalSpeech, -20.0, -8.0, -45.0));
    auto minimalPlan = OptimizationEngine::buildPlan(minimal);
    bool minimalMentionsKeyboard = std::any_of(
        minimalPlan.recommendations.begin(), minimalPlan.recommendations.end(),
        [](const auto &s) { return s.find("keyboard") != std::string::npos; });
    CHECK(!minimalMentionsKeyboard);
}

TEST_CASE(RecommendedFilterOrderMatchesSignalChainDesign) {
    auto order = recommendedFilterOrder();
    CHECK(order.size() == 6);
    CHECK(order[0] == "noise_suppress_filter");
    CHECK(order[1] == "noise_gate_filter");
    CHECK(order[2] == "expander_filter");
    CHECK(order[3] == "compressor_filter");
    CHECK(order[4] == "limiter_filter");
    CHECK(order[5] == "gain_filter");
}

TEST_CASE(FilterPlanJsonContainsExpectedKeys) {
    auto plan = OptimizationEngine::buildPlan(makeCleanProfile());
    std::string json = plan.toJson();
    CHECK(json.find("\"noiseSuppress\"") != std::string::npos);
    CHECK(json.find("\"gate\"") != std::string::npos);
    CHECK(json.find("\"expander\"") != std::string::npos);
    CHECK(json.find("\"compressor\"") != std::string::npos);
    CHECK(json.find("\"limiter\"") != std::string::npos);
    CHECK(json.find("\"gain\"") != std::string::npos);
    CHECK(json.find("\"recommendations\"") != std::string::npos);
}
