#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace aiaudio {

enum class CalibrationStage {
    NormalSpeech,
    LoudSpeech,
    QuietSpeech,
    Silence,
    Clapping,
    Keyboard,
    Mouse,
    Breathing,
    Count // sentinel: "no more stages", not a real stage
};

enum class StageCompletionMode {
    Duration,   // stage finishes after a fixed amount of audio has been captured
    EventCount, // stage finishes once N transient events are detected (e.g. 3 claps)
};

struct StageDefinition {
    CalibrationStage stage;
    const char *name;
    const char *instruction; // what the AI tells the user to do
    const char *purpose;     // what this stage is used for
    StageCompletionMode completionMode;
    double durationSeconds; // Duration mode: target length. EventCount mode: safety cap.
    size_t requiredEvents;  // only meaningful for EventCount mode
};

const StageDefinition &getStageDefinition(CalibrationStage stage);
CalibrationStage firstStage();
CalibrationStage nextStage(CalibrationStage stage); // returns CalibrationStage::Count after the last stage

// All 8 real stages in order (excludes the Count sentinel). Lets UI code
// build a full walkthrough/checklist without duplicating the stage table.
const std::vector<CalibrationStage> &allStages();

} // namespace aiaudio
