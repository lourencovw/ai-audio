#pragma once
#include "AudioTypes.h"
#include <cstddef>
#include <vector>

namespace aiaudio {

struct TransientDetectorConfig {
    double thresholdAboveFloorDb = 15.0; // rising-edge trigger margin above the tracked background level
    double releaseMarginDb = 6.0;        // envelope must fall back below (trigger - this margin) before a new transient can be counted
    double envelopeMs = 5.0;             // envelope follower block size
};

// Detects discrete, short-duration transient events (claps, keystrokes,
// mouse clicks) using a rising-edge/hysteresis state machine over a fast
// envelope follower and a slowly adapting background level. A transient is
// only re-armed once the envelope falls back near the background, so a
// single sustained loud tone or speech burst is counted once rather than
// many times. This is a deterministic, rule-based detector (per the MVP1
// "measurements and rules, not ML" approach), not a learned classifier.
class TransientDetector {
public:
    explicit TransientDetector(uint32_t sampleRate, TransientDetectorConfig config = {});

    void feed(const float *samples, size_t count);
    size_t transientCount() const { return transientCount_; }

private:
    uint32_t sampleRate_;
    TransientDetectorConfig config_;
    size_t envelopeBlockSize_;

    std::vector<float> blockBuffer_;
    double backgroundDbfs_ = kSilenceFloorDbfs;
    bool warmedUp_ = false;
    bool active_ = false;
    size_t transientCount_ = 0;
};

} // namespace aiaudio
