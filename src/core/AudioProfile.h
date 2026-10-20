#pragma once
#include "AudioAnalyzer.h"
#include "CalibrationStage.h"
#include "FFTAnalyzer.h"
#include <string>
#include <vector>

namespace aiaudio {

struct StageResult {
    CalibrationStage stage;
    LevelStats level;
    SpectralStats spectral;
    size_t transientCount = 0;
    double capturedDurationSeconds = 0.0;
};

class AudioProfile {
public:
    void addStageResult(StageResult result);
    const std::vector<StageResult> &stageResults() const { return stages_; }
    const StageResult *findStage(CalibrationStage stage) const;

    // Serializes the full profile to a JSON document. Hand-written rather
    // than pulling in a third-party JSON library, since the schema is small
    // and fixed; downstream consumers (the future optimization engine, the
    // OBS UI, or an external script) only need to read it.
    std::string toJson() const;

private:
    std::vector<StageResult> stages_;
};

} // namespace aiaudio
