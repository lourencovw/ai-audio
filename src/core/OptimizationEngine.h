#pragma once
#include "AudioProfile.h"
#include <string>
#include <vector>

namespace aiaudio {

// Settings mirror the exact obs_data keys used by OBS Studio's built-in
// "obs-filters" plugin (verified against obs-studio 32.2.2 source), so a
// FilterPlan can be applied directly via obs_source_create()/
// obs_data_set_*() without any additional translation layer.

struct NoiseSuppressSettings {
    int suppressLevelDb = -30; // S_SUPPRESS_LEVEL, more negative = more aggressive
    std::string method = "rnnoise"; // S_METHOD: "rnnoise" or "speex"
};

struct GateSettings {
    double openThresholdDb = -26.0;  // S_OPEN_THRESHOLD
    double closeThresholdDb = -32.0; // S_CLOSE_THRESHOLD
    int attackTimeMs = 25;           // S_ATTACK_TIME
    int holdTimeMs = 200;            // S_HOLD_TIME
    int releaseTimeMs = 150;         // S_RELEASE_TIME
};

struct ExpanderSettings {
    double thresholdDb = -40.0; // S_FILTER_THRESHOLD
    double ratio = 2.0;         // S_RATIO ("expander" preset: gentle, ~2:1)
    int attackTimeMs = 10;      // S_ATTACK_TIME
    int releaseTimeMs = 50;     // S_RELEASE_TIME
    double outputGainDb = 0.0;  // S_OUTPUT_GAIN
};

struct CompressorSettings {
    double thresholdDb = -18.0; // S_FILTER_THRESHOLD
    double ratio = 3.0;         // S_FILTER_RATIO
    int attackTimeMs = 6;       // S_ATTACK_TIME
    int releaseTimeMs = 60;     // S_RELEASE_TIME
    double outputGainDb = 0.0;  // S_OUTPUT_GAIN
};

struct LimiterSettings {
    double thresholdDb = -6.0; // S_FILTER_THRESHOLD
    int releaseTimeMs = 60;    // S_RELEASE_TIME
};

struct GainSettings {
    double gainDb = 0.0; // S_GAIN_DB
};

struct FilterPlan {
    NoiseSuppressSettings noiseSuppress;
    GateSettings gate;
    ExpanderSettings expander;
    CompressorSettings compressor;
    LimiterSettings limiter;
    GainSettings gain;

    // Human-readable notes explaining what was configured and why, in the
    // spirit of the product spec's "AI recommendations" report (section
    // 2.D) -- generated from the actual measurements, not canned text.
    std::vector<std::string> recommendations;

    std::string toJson() const;
};

// Translates a finished AudioProfile into concrete OBS filter parameter
// values using deterministic rules (per the MVP1 "measurements and rules,
// not ML" approach): thresholds are derived from the measured noise floor
// and speech levels, compressor ratio from the measured dynamic range
// between quiet and loud speech, gain from the gap to a target speaking
// level, etc.
class OptimizationEngine {
public:
    static FilterPlan buildPlan(const AudioProfile &profile);
};

// The order filters should be added to a source's filter chain in, so the
// overall signal path is: clean up broadband noise, remove residual
// low-level noise naturally, even out speech dynamics, catch any
// remaining peaks, then trim to a consistent final output level. Matches
// the obs_data_filter_id strings used by OBS's built-in obs-filters
// plugin.
std::vector<std::string> recommendedFilterOrder();

} // namespace aiaudio
