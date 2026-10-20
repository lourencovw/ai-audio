#pragma once

// This header is part of the OBS Studio integration layer, which requires
// libobs headers and an import library. See docs/CALIBRATION_ENGINE.md
// for build instructions.
// The core calibration engine in src/core has no OBS dependency and is
// built/unit-tested independently (tools/build.ps1, tests/).

// Registers the "AI Audio Engineer - Calibration" audio filter source type
// with OBS. Call once from obs_module_load().
void register_calibration_filter();
