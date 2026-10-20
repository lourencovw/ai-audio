#include "OptimizationEngine.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace aiaudio {

namespace {

double clampD(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

// Linearly maps `value` from [inLo, inHi] to [outLo, outHi], clamping the
// input first. Used for the noise-suppression-strength heuristic below.
double lerpClamped(double value, double inLo, double inHi, double outLo, double outHi) {
    double t = inHi > inLo ? (value - inLo) / (inHi - inLo) : 0.0;
    t = clampD(t, 0.0, 1.0);
    return outLo + t * (outHi - outLo);
}

// A reasonable, broadcast-style target for average mic level. Chosen to
// leave headroom below 0 dBFS for compression/limiting while still being
// clearly audible.
constexpr double kTargetSpeechDbfs = -18.0;

} // namespace

std::vector<std::string> recommendedFilterOrder() {
    return {"noise_suppress_filter", "noise_gate_filter", "expander_filter",
             "compressor_filter",     "limiter_filter",    "gain_filter"};
}

FilterPlan OptimizationEngine::buildPlan(const AudioProfile &profile) {
    FilterPlan plan;

    const StageResult *normal = profile.findStage(CalibrationStage::NormalSpeech);
    const StageResult *loud = profile.findStage(CalibrationStage::LoudSpeech);
    const StageResult *quiet = profile.findStage(CalibrationStage::QuietSpeech);
    const StageResult *silence = profile.findStage(CalibrationStage::Silence);
    const StageResult *keyboard = profile.findStage(CalibrationStage::Keyboard);
    const StageResult *mouse = profile.findStage(CalibrationStage::Mouse);
    const StageResult *breathing = profile.findStage(CalibrationStage::Breathing);

    // Fall back to OBS's own stock defaults for any stage that wasn't
    // captured (e.g. a plan requested from a partial profile), so the
    // plan is always safe to apply.
    double noiseFloor = silence ? silence->level.noiseFloorDbfs : -50.0;
    double quietLevel = quiet ? quiet->level.rmsDbfsAvg : -30.0;
    double normalLevel = normal ? normal->level.rmsDbfsAvg : -20.0;
    double loudLevel = loud ? loud->level.rmsDbfsAvg : -12.0;
    double loudPeak = loud ? loud->level.peakDbfs : -3.0;

    // --- Noise Gate: mute/attenuate between words based on the measured
    // ambient noise floor, while staying safely below quiet speech so
    // quiet words still open the gate. ---
    {
        const double kOpenMarginAboveNoiseDb = 10.0;
        const double kSpeechSafetyMarginDb = 6.0;
        const double kHysteresisDb = 6.0;

        double desiredOpen = noiseFloor + kOpenMarginAboveNoiseDb;
        double speechSafetyBound = quietLevel - kSpeechSafetyMarginDb;

        double openThreshold;
        if (speechSafetyBound < desiredOpen) {
            // Noise floor is close to the quiet-speech level: not enough
            // headroom to separate them cleanly.
            openThreshold = (noiseFloor + quietLevel) / 2.0;
            plan.recommendations.push_back(
                "Background noise is close to your quiet speaking volume, so the noise gate's margin is "
                "tight. Move the microphone closer or reduce room noise for more reliable gating.");
        } else {
            openThreshold = desiredOpen;
        }
        openThreshold = clampD(openThreshold, -60.0, -10.0);

        plan.gate.openThresholdDb = openThreshold;
        plan.gate.closeThresholdDb = openThreshold - kHysteresisDb;
    }

    // --- Expander: gentler, continuous reduction of low-level sounds
    // (just below typical quiet speech) rather than a hard on/off gate. ---
    {
        double threshold = clampD(quietLevel - 4.0, noiseFloor + 4.0, -15.0);
        plan.expander.thresholdDb = threshold;
        plan.expander.ratio = 2.0;
    }

    // --- Compressor: engage between normal and loud speech, with a ratio
    // scaled to the measured dynamic range (louder swings => more
    // compression, within a conservative range so speech doesn't pump or
    // sound unnatural). ---
    {
        double dynamicRange = std::max(0.0, loudLevel - quietLevel);
        plan.compressor.thresholdDb = clampD((normalLevel + loudLevel) / 2.0, -40.0, -6.0);
        plan.compressor.ratio = clampD(2.0 + (dynamicRange - 10.0) / 10.0 * 1.5, 2.0, 6.0);
    }

    // --- Limiter: catch peaks just above the user's measured loud-speech
    // peak, rather than an arbitrary fixed ceiling. ---
    {
        plan.limiter.thresholdDb = clampD(loudPeak - 1.0, -12.0, -1.0);
    }

    // --- Gain: bring the *normal* speaking level up/down to a consistent
    // target, so quiet mics get boosted and hot mics get turned down. ---
    {
        double gainDb = clampD(kTargetSpeechDbfs - normalLevel, -24.0, 24.0);
        plan.gain.gainDb = gainDb;
        std::ostringstream note;
        note << std::fixed << std::setprecision(1);
        note << "Microphone gain adjusted by " << (gainDb >= 0 ? "+" : "") << gainDb
             << " dB to bring normal speech to a consistent target level (" << kTargetSpeechDbfs << " dBFS).";
        plan.recommendations.push_back(note.str());
    }

    // --- Noise Suppression: strength scaled to the measured
    // signal-to-noise ratio of quiet speech vs. the ambient noise floor.
    // Clean recordings (high SNR) get gentle suppression to avoid
    // damaging consonants/making speech sound robotic; noisy recordings
    // (low SNR) get more aggressive suppression. ---
    {
        const double kSnrNoisyDb = 10.0;   // at/below this, use max suppression
        const double kSnrCleanDb = 35.0;   // at/above this, use min suppression
        const int kSuppressAggressiveDb = -30;
        const int kSuppressGentleDb = -10;

        double snr = quietLevel - noiseFloor;
        double suppressLevel = lerpClamped(snr, kSnrNoisyDb, kSnrCleanDb, kSuppressAggressiveDb, kSuppressGentleDb);
        plan.noiseSuppress.suppressLevelDb = static_cast<int>(std::round(suppressLevel));

        std::ostringstream note;
        note << std::fixed << std::setprecision(1);
        note << "Noise suppression set to " << plan.noiseSuppress.suppressLevelDb << " dB based on a measured "
             << snr << " dB gap between quiet speech and the ambient noise floor.";
        plan.recommendations.push_back(note.str());
    }

    {
        std::ostringstream note;
        note << std::fixed << std::setprecision(1);
        note << "Compressor set to a " << plan.compressor.ratio << ":1 ratio based on your measured dynamic range "
             << "between quiet and loud speech.";
        plan.recommendations.push_back(note.str());
    }

    if (keyboard && keyboard->transientCount > 0) {
        std::ostringstream note;
        note << "Detected keyboard noise during the typing test (" << keyboard->transientCount
             << " keystrokes) -- the noise gate and expander will attenuate it between words.";
        plan.recommendations.push_back(note.str());
    }

    if (mouse && mouse->transientCount > 0) {
        std::ostringstream note;
        note << "Detected mechanical click noise during the mouse test (" << mouse->transientCount
             << " clicks) -- covered by the same gate/expander settings as keyboard noise.";
        plan.recommendations.push_back(note.str());
    }

    if (breathing) {
        plan.recommendations.push_back(
            "Breathing captured during calibration. This MVP attenuates it only as part of general "
            "low-level noise reduction; a dedicated breath-detection mode is a planned future "
            "enhancement, not yet implemented.");
    }

    return plan;
}

namespace {

void appendKeyValue(std::ostringstream &os, const char *key, double value, bool trailingComma = true) {
    os << "    \"" << key << "\": " << value;
    if (trailingComma) os << ",";
    os << "\n";
}

void appendKeyValue(std::ostringstream &os, const char *key, int value, bool trailingComma = true) {
    os << "    \"" << key << "\": " << value;
    if (trailingComma) os << ",";
    os << "\n";
}

void appendKeyValue(std::ostringstream &os, const char *key, const std::string &value, bool trailingComma = true) {
    os << "    \"" << key << "\": \"" << value << "\"";
    if (trailingComma) os << ",";
    os << "\n";
}

} // namespace

std::string FilterPlan::toJson() const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{\n";

    os << "  \"noiseSuppress\": {\n";
    appendKeyValue(os, "suppressLevelDb", noiseSuppress.suppressLevelDb);
    appendKeyValue(os, "method", noiseSuppress.method, false);
    os << "  },\n";

    os << "  \"gate\": {\n";
    appendKeyValue(os, "openThresholdDb", gate.openThresholdDb);
    appendKeyValue(os, "closeThresholdDb", gate.closeThresholdDb);
    appendKeyValue(os, "attackTimeMs", gate.attackTimeMs);
    appendKeyValue(os, "holdTimeMs", gate.holdTimeMs);
    appendKeyValue(os, "releaseTimeMs", gate.releaseTimeMs, false);
    os << "  },\n";

    os << "  \"expander\": {\n";
    appendKeyValue(os, "thresholdDb", expander.thresholdDb);
    appendKeyValue(os, "ratio", expander.ratio);
    appendKeyValue(os, "attackTimeMs", expander.attackTimeMs);
    appendKeyValue(os, "releaseTimeMs", expander.releaseTimeMs);
    appendKeyValue(os, "outputGainDb", expander.outputGainDb, false);
    os << "  },\n";

    os << "  \"compressor\": {\n";
    appendKeyValue(os, "thresholdDb", compressor.thresholdDb);
    appendKeyValue(os, "ratio", compressor.ratio);
    appendKeyValue(os, "attackTimeMs", compressor.attackTimeMs);
    appendKeyValue(os, "releaseTimeMs", compressor.releaseTimeMs);
    appendKeyValue(os, "outputGainDb", compressor.outputGainDb, false);
    os << "  },\n";

    os << "  \"limiter\": {\n";
    appendKeyValue(os, "thresholdDb", limiter.thresholdDb);
    appendKeyValue(os, "releaseTimeMs", limiter.releaseTimeMs, false);
    os << "  },\n";

    os << "  \"gain\": {\n";
    appendKeyValue(os, "gainDb", gain.gainDb, false);
    os << "  },\n";

    os << "  \"recommendations\": [\n";
    for (size_t i = 0; i < recommendations.size(); ++i) {
        os << "    \"" << recommendations[i] << "\"" << (i + 1 < recommendations.size() ? "," : "") << "\n";
    }
    os << "  ]\n";

    os << "}\n";
    return os.str();
}

} // namespace aiaudio
