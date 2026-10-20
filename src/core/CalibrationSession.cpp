#include "CalibrationSession.h"
#include <algorithm>

namespace aiaudio {

CalibrationSession::CalibrationSession(uint32_t sampleRate)
    : sampleRate_(sampleRate), currentStage_(firstStage()) {
    startStage(currentStage_);
}

void CalibrationSession::startStage(CalibrationStage) {
    stageSamplesFed_ = 0;
    stageStarted_ = false;
    levelAnalyzer_ = std::make_unique<AudioAnalyzer>(sampleRate_);
    fftAnalyzer_ = std::make_unique<FFTAnalyzer>(sampleRate_);
    transientDetector_ = std::make_unique<TransientDetector>(sampleRate_);
}

void CalibrationSession::startCurrentStage() {
    if (isComplete()) return;
    startStage(currentStage_); // fresh analyzers, so this also safely "redoes" an active stage
    stageStarted_ = true;
}

double CalibrationSession::currentStageElapsedSeconds() const {
    return static_cast<double>(stageSamplesFed_) / static_cast<double>(sampleRate_);
}

size_t CalibrationSession::currentStageTransientCount() const {
    return transientDetector_ ? transientDetector_->transientCount() : 0;
}

bool CalibrationSession::stageFinished() const {
    const auto &def = getStageDefinition(currentStage_);
    double elapsedSeconds = static_cast<double>(stageSamplesFed_) / static_cast<double>(sampleRate_);

    if (def.completionMode == StageCompletionMode::EventCount) {
        bool haveEnoughEvents = transientDetector_->transientCount() >= def.requiredEvents;
        bool hitSafetyCap = elapsedSeconds >= def.durationSeconds;
        return haveEnoughEvents || hitSafetyCap;
    }

    return elapsedSeconds >= def.durationSeconds;
}

void CalibrationSession::finishCurrentStage() {
    StageResult result;
    result.stage = currentStage_;
    result.level = levelAnalyzer_->finish();
    result.spectral = fftAnalyzer_->finish();
    result.transientCount = transientDetector_->transientCount();
    result.capturedDurationSeconds = static_cast<double>(stageSamplesFed_) / static_cast<double>(sampleRate_);
    profile_.addStageResult(std::move(result));

    currentStage_ = nextStage(currentStage_);
    if (currentStage_ != CalibrationStage::Count) {
        startStage(currentStage_);
    }
    if (onStageChanged_) onStageChanged_(currentStage_);
}

size_t CalibrationSession::samplesUntilDurationBoundary() const {
    const auto &def = getStageDefinition(currentStage_);
    size_t targetSamples = static_cast<size_t>(def.durationSeconds * sampleRate_);
    return stageSamplesFed_ < targetSamples ? targetSamples - stageSamplesFed_ : 0;
}

void CalibrationSession::feed(const float *samples, size_t count) {
    size_t offset = 0;
    while (offset < count && !isComplete() && stageStarted_) {
        size_t available = count - offset;
        size_t toConsume = available;

        // For duration-based stages, never consume past the exact stage
        // boundary in one go, so any leftover is attributed to the next
        // stage instead of the one that just finished.
        if (getStageDefinition(currentStage_).completionMode == StageCompletionMode::Duration) {
            toConsume = std::min(available, samplesUntilDurationBoundary());
            if (toConsume == 0) toConsume = available; // shouldn't happen, but avoid an infinite loop
        }

        levelAnalyzer_->feed(samples + offset, toConsume);
        fftAnalyzer_->feed(samples + offset, toConsume);
        transientDetector_->feed(samples + offset, toConsume);
        stageSamplesFed_ += toConsume;
        offset += toConsume;

        // finishCurrentStage() resets stageStarted_ to false for the new
        // stage (via startStage()), so the loop condition above naturally
        // stops consuming further once a stage finishes -- the next stage
        // requires its own explicit startCurrentStage() call.
        if (stageFinished()) finishCurrentStage();
    }
}

} // namespace aiaudio
