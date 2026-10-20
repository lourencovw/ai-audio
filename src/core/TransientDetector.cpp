#include "TransientDetector.h"
#include "AudioAnalyzer.h" // for linearToDbfs
#include <algorithm>
#include <cmath>

namespace aiaudio {

TransientDetector::TransientDetector(uint32_t sampleRate, TransientDetectorConfig config)
    : sampleRate_(sampleRate),
      config_(config),
      envelopeBlockSize_(std::max<size_t>(1, static_cast<size_t>(sampleRate * config.envelopeMs / 1000.0))) {
    blockBuffer_.reserve(envelopeBlockSize_);
}

void TransientDetector::feed(const float *samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        blockBuffer_.push_back(samples[i]);
        if (blockBuffer_.size() < envelopeBlockSize_) continue;

        double peak = 0.0;
        for (float s : blockBuffer_) peak = std::max(peak, static_cast<double>(std::fabs(s)));
        double envDbfs = linearToDbfs(peak);
        blockBuffer_.clear();

        if (!warmedUp_) {
            backgroundDbfs_ = envDbfs;
            warmedUp_ = true;
            continue;
        }

        double triggerLevel = backgroundDbfs_ + config_.thresholdAboveFloorDb;
        double releaseLevel = triggerLevel - config_.releaseMarginDb;

        if (!active_ && envDbfs > triggerLevel) {
            ++transientCount_;
            active_ = true;
        } else if (active_ && envDbfs < releaseLevel) {
            active_ = false;
        }

        if (!active_) {
            // Slowly track the background level (exponential smoothing) only
            // while idle, so a loud event doesn't drag the floor upward.
            const double kSmoothing = 0.05;
            backgroundDbfs_ = backgroundDbfs_ + kSmoothing * (envDbfs - backgroundDbfs_);
        }
    }
}

} // namespace aiaudio
