#pragma once
#include "AudioAnalyzer.h"
#include "AudioProfile.h"
#include "FFTAnalyzer.h"
#include "TransientDetector.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace aiaudio {

// Drives the guided calibration flow described in the product spec: feeds
// a continuous stream of mono audio samples, automatically segments it
// into the eight calibration stages, and produces a finished AudioProfile
// once every stage has completed.
//
// Each stage requires an explicit startCurrentStage() call (e.g. from a
// "Start" button in the UI) before any audio is captured for it --
// feed() is a no-op until then. This gives the user control over exactly
// when a timed stage (or the clap counter) begins, rather than silently
// starting to count the moment audio starts flowing through the filter.
//
// feed() splits an input chunk exactly at a duration-based stage boundary
// (e.g. the 10s Normal Speech stage), so audio fed after that boundary is
// always attributed to the new stage even if the caller passes in one
// large buffer that spans multiple stages. This matters most for the
// Silence stage: without exact splitting, a user who keeps talking for a
// moment after their timer ends would otherwise contaminate the
// environmental noise-floor measurement.
//
// Known simplification: event-count-based stages (currently only
// Clapping) are not split as precisely -- completion is only detected
// after a whole chunk has been processed by the transient detector, so up
// to one chunk's worth of trailing audio can spill into the next stage.
// This is negligible for the small chunk sizes a real-time audio callback
// uses (e.g. 10-20ms).
class CalibrationSession {
public:
    using StageChangedCallback = std::function<void(CalibrationStage newStage)>;

    explicit CalibrationSession(uint32_t sampleRate);

    // Feeds the next chunk of mono float samples (range [-1, 1]). Ignored
    // (not just for the "belongs to a finished session" case, but also)
    // while the current stage hasn't been started yet via
    // startCurrentStage() -- see isWaitingToStart(). Safe to call with
    // arbitrarily sized chunks (e.g. one OBS audio callback's worth of
    // frames at a time).
    void feed(const float *samples, size_t count);

    CalibrationStage currentStage() const { return currentStage_; }
    bool isComplete() const { return currentStage_ == CalibrationStage::Count; }

    // True once the previous stage (or session start) has set up the
    // current stage but it hasn't begun capturing audio yet -- i.e. the UI
    // should show a "Start" affordance and feed() will ignore audio.
    bool isWaitingToStart() const { return !isComplete() && !stageStarted_; }

    // True while the current stage is actively capturing audio.
    bool isStageActive() const { return !isComplete() && stageStarted_; }

    // (Re)starts capture for the current stage from zero: resets its
    // analyzers/counters and begins accepting audio via feed(). Safe to
    // call again while already active, to redo the current stage (e.g.
    // the user wasn't ready and wants to restart the countdown/clap
    // count). No-op once the whole session is complete.
    void startCurrentStage();

    // Live progress for the UI, valid once isStageActive() is true (both
    // return 0 beforehand).
    double currentStageElapsedSeconds() const;
    size_t currentStageTransientCount() const;

    // Invoked once, synchronously from feed(), the moment a stage finishes
    // and the session moves on to the next one (or completes). Called with
    // CalibrationStage::Count when the whole session finishes.
    void setStageChangedCallback(StageChangedCallback callback) { onStageChanged_ = std::move(callback); }

    // Only meaningful once isComplete() is true, but safe to call anytime
    // (returns whatever stages have finished so far).
    const AudioProfile &profile() const { return profile_; }

private:
    void startStage(CalibrationStage stage);
    bool stageFinished() const;
    void finishCurrentStage();
    size_t samplesUntilDurationBoundary() const;

    uint32_t sampleRate_;
    CalibrationStage currentStage_;
    bool stageStarted_ = false;
    StageChangedCallback onStageChanged_;

    std::unique_ptr<AudioAnalyzer> levelAnalyzer_;
    std::unique_ptr<FFTAnalyzer> fftAnalyzer_;
    std::unique_ptr<TransientDetector> transientDetector_;
    size_t stageSamplesFed_ = 0;

    AudioProfile profile_;
};

} // namespace aiaudio
