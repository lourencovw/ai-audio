// OBS Studio audio-filter integration for the calibration engine.
//
// Built and verified against the real obs-studio 32.2.2 source/headers
// (see docs/CALIBRATION_ENGINE.md for how). It is a standard OBS filter
// plugin (obs_source_info with OBS_SOURCE_TYPE_FILTER / OBS_SOURCE_AUDIO
// and a filter_audio callback).
#include "calibration-filter.h"

#include <media-io/audio-io.h>
#include <obs-module.h>

#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "CalibrationSession.h"
#include "CalibrationStage.h"
#include "OptimizationEngine.h"

using aiaudio::CalibrationSession;
using aiaudio::CalibrationStage;
using aiaudio::FilterPlan;
using aiaudio::getStageDefinition;
using aiaudio::OptimizationEngine;
using aiaudio::StageCompletionMode;

namespace {

struct CalibrationFilterData {
    obs_source_t *source = nullptr; // the calibration filter itself
    std::unique_ptr<CalibrationSession> session;
    std::vector<float> monoScratch;
    std::string profilePath;
    bool restartRequested = false;
    std::string lastRefreshSignature; // throttles obs_source_update_properties() calls
};

void start_new_session(CalibrationFilterData *filter, uint32_t sampleRate);

const char *filter_get_name(void *) { return "AI Audio Engineer - Calibration"; }

void filter_update(void *data, obs_data_t *settings) {
    auto *filter = static_cast<CalibrationFilterData *>(data);
    const char *path = obs_data_get_string(settings, "profile_path");
    filter->profilePath = path ? path : "";
}

void *filter_create(obs_data_t *settings, obs_source_t *source) {
    auto *filter = new CalibrationFilterData();
    filter->source = source;
    filter_update(filter, settings);

    // Create the session up-front (rather than lazily on first audio
    // callback) so the properties dialog has something to show the
    // instant the user opens it, even before any audio has flowed.
    obs_audio_info info;
    if (obs_get_audio_info(&info)) start_new_session(filter, info.samples_per_sec);

    return filter;
}

void filter_destroy(void *data) { delete static_cast<CalibrationFilterData *>(data); }

void filter_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "profile_path", "ai-audio-profile.json");
}

// --- Translating a FilterPlan into real OBS filters on the parent source ---
//
// Setting keys below are the exact obs_data keys used by OBS Studio's
// built-in obs-filters plugin (verified against the obs-studio 32.2.2
// source for compressor_filter, expander_filter, noise_gate_filter,
// limiter_filter, gain_filter, noise_suppress_filter).

obs_data_t *build_noise_suppress_settings(const aiaudio::NoiseSuppressSettings &s) {
    obs_data_t *d = obs_data_create();
    obs_data_set_int(d, "suppress_level", s.suppressLevelDb);
    obs_data_set_string(d, "method", s.method.c_str());
    return d;
}

obs_data_t *build_gate_settings(const aiaudio::GateSettings &s) {
    obs_data_t *d = obs_data_create();
    obs_data_set_double(d, "open_threshold", s.openThresholdDb);
    obs_data_set_double(d, "close_threshold", s.closeThresholdDb);
    obs_data_set_int(d, "attack_time", s.attackTimeMs);
    obs_data_set_int(d, "hold_time", s.holdTimeMs);
    obs_data_set_int(d, "release_time", s.releaseTimeMs);
    return d;
}

obs_data_t *build_expander_settings(const aiaudio::ExpanderSettings &s) {
    obs_data_t *d = obs_data_create();
    obs_data_set_double(d, "threshold", s.thresholdDb);
    obs_data_set_double(d, "ratio", s.ratio);
    obs_data_set_int(d, "attack_time", s.attackTimeMs);
    obs_data_set_int(d, "release_time", s.releaseTimeMs);
    obs_data_set_double(d, "output_gain", s.outputGainDb);
    obs_data_set_string(d, "presets", "expander");
    obs_data_set_string(d, "detector", "RMS");
    return d;
}

obs_data_t *build_compressor_settings(const aiaudio::CompressorSettings &s) {
    obs_data_t *d = obs_data_create();
    obs_data_set_double(d, "ratio", s.ratio);
    obs_data_set_double(d, "threshold", s.thresholdDb);
    obs_data_set_int(d, "attack_time", s.attackTimeMs);
    obs_data_set_int(d, "release_time", s.releaseTimeMs);
    obs_data_set_double(d, "output_gain", s.outputGainDb);
    return d;
}

obs_data_t *build_limiter_settings(const aiaudio::LimiterSettings &s) {
    obs_data_t *d = obs_data_create();
    obs_data_set_double(d, "threshold", s.thresholdDb);
    obs_data_set_int(d, "release_time", s.releaseTimeMs);
    return d;
}

obs_data_t *build_gain_settings(const aiaudio::GainSettings &s) {
    obs_data_t *d = obs_data_create();
    obs_data_set_double(d, "db", s.gainDb);
    return d;
}

// Creates/replaces the six AI-Audio-Engineer-managed filters on `parent`,
// in the signal-chain order defined by
// aiaudio::recommendedFilterOrder(). Re-running calibration simply
// replaces the previous filters by name, so this is safe to call more
// than once on the same source.
void apply_filter_plan(obs_source_t *parent, const FilterPlan &plan) {
    struct Entry {
        const char *id;
        const char *name;
        obs_data_t *settings;
    };

    // NOTE: order here must match aiaudio::recommendedFilterOrder() (unit
    // tested in tests/test_optimization_engine.cpp).
    Entry entries[] = {
        {"noise_suppress_filter", "AI Audio Engineer - Noise Suppression",
         build_noise_suppress_settings(plan.noiseSuppress)},
        {"noise_gate_filter", "AI Audio Engineer - Noise Gate", build_gate_settings(plan.gate)},
        {"expander_filter", "AI Audio Engineer - Expander", build_expander_settings(plan.expander)},
        {"compressor_filter", "AI Audio Engineer - Compressor", build_compressor_settings(plan.compressor)},
        {"limiter_filter", "AI Audio Engineer - Limiter", build_limiter_settings(plan.limiter)},
        {"gain_filter", "AI Audio Engineer - Gain", build_gain_settings(plan.gain)},
    };

    for (auto &entry : entries) {
        obs_source_t *existing = obs_source_get_filter_by_name(parent, entry.name);
        if (existing) {
            obs_source_filter_remove(parent, existing);
            obs_source_release(existing);
        }

        obs_source_t *filterSource = obs_source_create(entry.id, entry.name, entry.settings, nullptr);
        obs_data_release(entry.settings);

        if (filterSource) {
            obs_source_filter_add(parent, filterSource);
            obs_source_release(filterSource);
        } else {
            blog(LOG_WARNING, "[ai-audio-engineer] Failed to create filter '%s' (id=%s)", entry.name, entry.id);
        }
    }
}

bool restart_calibration_clicked(obs_properties_t *, obs_property_t *, void *data) {
    auto *filter = static_cast<CalibrationFilterData *>(data);
    filter->restartRequested = true;
    return true; // refresh the properties view
}

bool start_stage_clicked(obs_properties_t *, obs_property_t *, void *data) {
    auto *filter = static_cast<CalibrationFilterData *>(data);
    if (filter->session && !filter->session->isComplete()) {
        filter->session->startCurrentStage();
        const auto &def = getStageDefinition(filter->session->currentStage());
        blog(LOG_INFO, "[ai-audio-engineer] Stage started: %s -- %s", def.name, def.instruction);
    }
    return true; // refresh the properties view to show "recording" state immediately
}

// Builds a numbered checklist of all 8 stages -- done ([x]), current
// ([>] waiting / [*] actively recording), or pending ([ ]) -- so the user
// can see the whole walkthrough at a glance, not just the current step.
std::string build_walkthrough_text(CalibrationSession *session) {
    std::ostringstream os;
    CalibrationStage current = session ? session->currentStage() : aiaudio::firstStage();
    bool complete = session && session->isComplete();
    bool reachedCurrent = false;

    int index = 0;
    for (auto stage : aiaudio::allStages()) {
        ++index;
        const auto &def = getStageDefinition(stage);

        const char *marker;
        if (complete) {
            marker = "[x]";
        } else if (stage == current) {
            marker = session->isStageActive() ? "[*]" : "[>]";
            reachedCurrent = true;
        } else if (!reachedCurrent) {
            marker = "[x]";
        } else {
            marker = "[ ]";
        }

        os << marker << " " << index << ". " << def.name;
        if (!complete && stage == current) os << " -- " << def.instruction;
        os << "\n";
    }
    return os.str();
}

// Builds the live status/progress line shown below the walkthrough:
// "ready to start", "recording, Xs / Ys", or "claps: X / 3", etc.
std::string build_status_text(CalibrationFilterData *filter) {
    if (!filter->session) return "Not started yet.";
    auto *session = filter->session.get();

    if (session->isComplete()) {
        return "Calibration complete -- Noise Suppression, Noise Gate, Expander, Compressor, Limiter, and "
               "Gain filters were applied to this source in that order. Click 'Restart Entire Calibration' "
               "below to redo it.";
    }

    const auto &def = getStageDefinition(session->currentStage());
    std::ostringstream os;
    os << std::fixed << std::setprecision(1);

    if (session->isWaitingToStart()) {
        os << "Ready: " << def.name << ". Click 'Start This Stage' below when you're ready.";
    } else if (def.completionMode == StageCompletionMode::EventCount) {
        os << "Recording \"" << def.name << "\" -- claps detected: " << session->currentStageTransientCount()
           << " / " << def.requiredEvents << " (will also stop automatically after " << std::setprecision(0)
           << def.durationSeconds << "s)";
    } else {
        os << "Recording \"" << def.name << "\" -- " << session->currentStageElapsedSeconds() << "s / "
           << def.durationSeconds << "s";
    }
    return os.str();
}

obs_properties_t *filter_properties(void *data) {
    auto *filter = static_cast<CalibrationFilterData *>(data);
    obs_properties_t *props = obs_properties_create();

    obs_properties_add_text(props, "walkthrough", build_walkthrough_text(filter->session.get()).c_str(),
                             OBS_TEXT_INFO);
    obs_properties_add_text(props, "status_info", build_status_text(filter).c_str(), OBS_TEXT_INFO);

    bool complete = filter->session && filter->session->isComplete();
    if (!complete) {
        obs_properties_add_button2(props, "start_button", "Start This Stage", start_stage_clicked, filter);
    }

    obs_properties_add_path(props, "profile_path", "Save profile + filter plan to", OBS_PATH_FILE_SAVE,
                             "JSON (*.json)", nullptr);
    obs_properties_add_button2(props, "restart_button", "Restart Entire Calibration", restart_calibration_clicked,
                                filter);
    return props;
}

void start_new_session(CalibrationFilterData *filter, uint32_t sampleRate) {
    filter->session = std::make_unique<CalibrationSession>(sampleRate);
    filter->lastRefreshSignature.clear();
    filter->session->setStageChangedCallback([filter](CalibrationStage newStage) {
        if (newStage == CalibrationStage::Count) {
            blog(LOG_INFO, "[ai-audio-engineer] Calibration complete; building and applying filter plan.");

            FilterPlan plan = OptimizationEngine::buildPlan(filter->session->profile());
            obs_source_t *parent = obs_filter_get_parent(filter->source);
            if (parent) {
                apply_filter_plan(parent, plan);
            } else {
                blog(LOG_WARNING, "[ai-audio-engineer] No parent source found; filters were not applied.");
            }

            for (const auto &note : plan.recommendations) blog(LOG_INFO, "[ai-audio-engineer] %s", note.c_str());

            if (!filter->profilePath.empty()) {
                std::ofstream out(filter->profilePath);
                out << "{\n  \"profile\": " << filter->session->profile().toJson() << ",\n  \"filterPlan\": "
                    << plan.toJson() << "}\n";
            }
        } else {
            const auto &def = getStageDefinition(newStage);
            blog(LOG_INFO, "[ai-audio-engineer] Ready: %s -- %s (click 'Start This Stage')", def.name,
                 def.instruction);
        }
    });
    blog(LOG_INFO, "[ai-audio-engineer] Calibration ready. First stage: %s -- %s",
         getStageDefinition(CalibrationStage::NormalSpeech).name,
         getStageDefinition(CalibrationStage::NormalSpeech).instruction);
}

// A compact string that changes whenever something the properties panel
// displays changes (stage, waiting/active, ~0.2s of elapsed time, or the
// live clap count), used to throttle obs_source_update_properties() calls
// to roughly 5/sec instead of once per audio buffer (~every 10ms).
std::string compute_display_signature(CalibrationSession *session) {
    if (!session) return "none";
    std::ostringstream os;
    if (session->isComplete()) {
        os << "complete";
    } else {
        os << static_cast<int>(session->currentStage()) << "|" << (session->isStageActive() ? "active" : "waiting");
        if (session->isStageActive()) {
            os << "|" << static_cast<int>(session->currentStageElapsedSeconds() * 5.0)
               << "|" << session->currentStageTransientCount();
        }
    }
    return os.str();
}

// Downmixes an OBS audio frame (planar float, up to MAX_AV_PLANES
// channels) to mono by averaging channels, then feeds it to the
// calibration session. The engine itself only ever reasons about mono
// audio (see AudioTypes.h).
obs_audio_data *filter_audio(void *data, obs_audio_data *audio) {
    auto *filter = static_cast<CalibrationFilterData *>(data);
    if (!filter->source || audio->frames == 0) return audio;

    obs_audio_info info;
    if (!obs_get_audio_info(&info)) return audio;

    if (filter->restartRequested) {
        filter->restartRequested = false;
        start_new_session(filter, info.samples_per_sec);
    }
    if (!filter->session) {
        start_new_session(filter, info.samples_per_sec);
    }

    if (filter->session->isComplete()) return audio;

    size_t channels = info.speakers == SPEAKERS_UNKNOWN ? 1 : get_audio_channels(info.speakers);
    channels = channels == 0 ? 1 : channels;

    filter->monoScratch.assign(audio->frames, 0.0f);
    for (size_t ch = 0; ch < channels && ch < MAX_AV_PLANES; ++ch) {
        const auto *samples = reinterpret_cast<const float *>(audio->data[ch]);
        if (!samples) continue;
        for (size_t i = 0; i < audio->frames; ++i) filter->monoScratch[i] += samples[i];
    }
    for (auto &s : filter->monoScratch) s /= static_cast<float>(channels);

    filter->session->feed(filter->monoScratch.data(), filter->monoScratch.size());

    std::string signature = compute_display_signature(filter->session.get());
    if (signature != filter->lastRefreshSignature) {
        filter->lastRefreshSignature = signature;
        obs_source_update_properties(filter->source);
    }

    return audio;
}

obs_source_info calibration_filter_info = [] {
    obs_source_info info = {};
    info.id = "ai_audio_calibration_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = filter_get_name;
    info.create = filter_create;
    info.destroy = filter_destroy;
    info.update = filter_update;
    info.get_defaults = filter_defaults;
    info.get_properties = filter_properties;
    info.filter_audio = filter_audio;
    return info;
}();

} // namespace

void register_calibration_filter() { obs_register_source(&calibration_filter_info); }
