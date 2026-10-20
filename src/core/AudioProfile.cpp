#include "AudioProfile.h"
#include <iomanip>
#include <sstream>

namespace aiaudio {

void AudioProfile::addStageResult(StageResult result) { stages_.push_back(std::move(result)); }

const StageResult *AudioProfile::findStage(CalibrationStage stage) const {
    for (const auto &s : stages_) {
        if (s.stage == stage) return &s;
    }
    return nullptr;
}

namespace {

std::string jsonEscape(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}

} // namespace

std::string AudioProfile::toJson() const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{\n  \"stages\": [\n";

    for (size_t i = 0; i < stages_.size(); ++i) {
        const auto &s = stages_[i];
        const auto &def = getStageDefinition(s.stage);
        os << "    {\n";
        os << "      \"stage\": \"" << jsonEscape(def.name) << "\",\n";
        os << "      \"capturedDurationSeconds\": " << s.capturedDurationSeconds << ",\n";
        os << "      \"level\": {\n";
        os << "        \"rmsDbfsAvg\": " << s.level.rmsDbfsAvg << ",\n";
        os << "        \"rmsDbfsMin\": " << s.level.rmsDbfsMin << ",\n";
        os << "        \"rmsDbfsMax\": " << s.level.rmsDbfsMax << ",\n";
        os << "        \"peakDbfs\": " << s.level.peakDbfs << ",\n";
        os << "        \"noiseFloorDbfs\": " << s.level.noiseFloorDbfs << "\n";
        os << "      },\n";
        os << "      \"spectral\": {\n";
        os << "        \"spectralCentroidHz\": " << s.spectral.spectralCentroidHz << ",\n";
        os << "        \"dominantFrequencyHz\": " << s.spectral.dominantFrequencyHz << ",\n";
        os << "        \"lowBandEnergyFraction\": " << s.spectral.lowBandEnergyFraction << ",\n";
        os << "        \"midBandEnergyFraction\": " << s.spectral.midBandEnergyFraction << ",\n";
        os << "        \"highBandEnergyFraction\": " << s.spectral.highBandEnergyFraction << "\n";
        os << "      },\n";
        os << "      \"transientCount\": " << s.transientCount << "\n";
        os << "    }" << (i + 1 < stages_.size() ? "," : "") << "\n";
    }

    os << "  ]\n}\n";
    return os.str();
}

} // namespace aiaudio
