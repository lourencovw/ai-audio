// OBS Studio plugin entry point. Requires libobs -- see
// src/obs-plugin/calibration-filter.cpp for details.
#include <obs-module.h>

#include "calibration-filter.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("ai-audio-engineer", "en-US")

bool obs_module_load(void) {
    register_calibration_filter();
    blog(LOG_INFO, "[ai-audio-engineer] plugin loaded (MVP1: calibration engine)");
    return true;
}

void obs_module_unload(void) { blog(LOG_INFO, "[ai-audio-engineer] plugin unloaded"); }
