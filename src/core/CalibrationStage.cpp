#include "CalibrationStage.h"
#include <array>
#include <stdexcept>

namespace aiaudio {

namespace {

// Mirrors the calibration test table from the product spec (section 2.A).
constexpr std::array<StageDefinition, 8> kStages{{
    {CalibrationStage::NormalSpeech, "Normal speech", "Speak naturally for 10 seconds.",
     "Establish normal voice levels and tonal characteristics", StageCompletionMode::Duration, 10.0, 0},
    {CalibrationStage::LoudSpeech, "Loud speech", "Speak as loudly as you normally would.",
     "Detect peaks and configure compression", StageCompletionMode::Duration, 10.0, 0},
    {CalibrationStage::QuietSpeech, "Quiet speech", "Speak softly for 10 seconds.",
     "Measure low-level speech and optimize expansion", StageCompletionMode::Duration, 10.0, 0},
    {CalibrationStage::Silence, "Silence", "Stay silent for 10 seconds.", "Analyze environmental noise",
     StageCompletionMode::Duration, 10.0, 0},
    {CalibrationStage::Clapping, "Clapping", "Clap your hands three times.",
     "Identify transient sounds and excessive gain", StageCompletionMode::EventCount, 15.0, 3},
    {CalibrationStage::Keyboard, "Keyboard typing", "Type on your keyboard for 10 seconds.",
     "Identify and suppress keyboard noise", StageCompletionMode::Duration, 10.0, 0},
    {CalibrationStage::Mouse, "Mouse clicking", "Click your mouse repeatedly.",
     "Identify mechanical background noise", StageCompletionMode::Duration, 8.0, 0},
    {CalibrationStage::Breathing, "Breathing", "Take a few normal breaths.",
     "Identify breathing sounds for attenuation", StageCompletionMode::Duration, 8.0, 0},
}};

} // namespace

const StageDefinition &getStageDefinition(CalibrationStage stage) {
    for (const auto &def : kStages) {
        if (def.stage == stage) return def;
    }
    throw std::out_of_range("Unknown calibration stage");
}

CalibrationStage firstStage() { return kStages.front().stage; }

CalibrationStage nextStage(CalibrationStage stage) {
    for (size_t i = 0; i < kStages.size(); ++i) {
        if (kStages[i].stage == stage) {
            return (i + 1 < kStages.size()) ? kStages[i + 1].stage : CalibrationStage::Count;
        }
    }
    return CalibrationStage::Count;
}

const std::vector<CalibrationStage> &allStages() {
    static const std::vector<CalibrationStage> stages = [] {
        std::vector<CalibrationStage> v;
        v.reserve(kStages.size());
        for (const auto &def : kStages) v.push_back(def.stage);
        return v;
    }();
    return stages;
}

} // namespace aiaudio
