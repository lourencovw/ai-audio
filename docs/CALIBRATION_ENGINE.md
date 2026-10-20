# MVP 1 — Design & Status

This document describes the AI Audio Engineer roadmap's first milestone:
**MVP 1** (product spec section 5), with the following components implemented:

1. **Calibration engine** (section 2.A/4): records the eight guided
   calibration tests and extracts RMS/peak/noise-floor/frequency
   measurements from each into a structured `AudioProfile`.
2. **Optimization engine** (section 2.C, "automatic audio filter
   configuration"): deterministically translates that profile into
   concrete OBS filter settings (Noise Suppression, Noise Gate, Expander,
   Compressor, Limiter, Gain).
3. **OBS integration**: an audio filter plugin that runs the calibration
   live against a source's audio, then automatically creates/configures
   those six filters on the same source when calibration finishes.

Not implemented (see "Remaining work" at the end): the AI Audio Quality
Report with numeric scores and before/after audio preview (section 2.D),
ReaPlugs integration, dedicated breath-removal mode, and a guided Qt UI
(stage instructions currently surface via the filter's properties panel
and the OBS log, not a dedicated wizard dialog).

## Why C++ with no external dependencies

The engine is built as a small, dependency-free static library
([src/core](../src/core)) so it can
be unit-tested on its own, independent of OBS, using synthetic signals
(sine waves, impulses, silence). The OBS filter
([src/obs-plugin](../src/obs-plugin))
is a thin adapter on top of it. JSON output and the FFT are both
hand-written rather than vendored from a third-party library, since the
scope (fixed schema, power-of-two windowed FFT) is small and this keeps the
whole engine buildable with nothing but a C++17 compiler.

## Stage table

Implemented 1:1 from the product spec's calibration test table
([CalibrationStage.cpp](../src/core/CalibrationStage.cpp)):

| Stage | Instruction | Completion | Purpose |
|---|---|---|---|
| Normal speech | "Speak naturally for 10 seconds." | 10s | Baseline voice levels/tone |
| Loud speech | "Speak as loudly as you normally would." | 10s | Peaks → compressor config |
| Quiet speech | "Speak softly for 10 seconds." | 10s | Low-level speech → expander config |
| Silence | "Stay silent for 10 seconds." | 10s | Environmental noise floor |
| Clapping | "Clap your hands three times." | 3 detected transients (15s safety cap) | Transient sounds / excessive gain |
| Keyboard typing | "Type on your keyboard for 10 seconds." | 10s | Keyboard noise signature |
| Mouse clicking | "Click your mouse repeatedly." | 8s | Mechanical noise signature |
| Breathing | "Take a few normal breaths." | 8s | Breathing signature for attenuation |

Stage metadata (instruction/purpose text, completion rule) lives in one
place so a future UI can render the guided flow directly from it rather
than duplicating strings.

## What gets measured per stage

Each finished stage produces a `StageResult`
([AudioProfile.h](../src/core/AudioProfile.h)):

- **Level** ([AudioAnalyzer](../src/core/AudioAnalyzer.h)): energy-averaged RMS (dBFS), min/max RMS across ~20ms analysis blocks, peak (dBFS), and a noise-floor estimate (10th percentile of block RMS — this generalizes "silence" to "the quiet gaps between words/claps/keystrokes" in any stage, not just the dedicated Silence stage).
- **Spectral** ([FFTAnalyzer](../src/core/FFTAnalyzer.h)): a Welch-averaged (many overlapping Hann-windowed FFTs, power-averaged) spectrum reduced to a spectral centroid, dominant frequency, and energy fraction in three bands — low (<300 Hz, rumble/hum/thuds), mid (300 Hz–3.4 kHz, voice band), high (>3.4 kHz, sibilance/hiss/clicks).
- **Transient count** ([TransientDetector](../src/core/TransientDetector.h)): count of discrete rising-edge events detected via a hysteresis state machine over a fast envelope follower and a slowly adapting background level. Used both to detect stage completion for Clapping (needs exactly 3) and as a general signature for Keyboard/Mouse stages (roughly the number of keystrokes/clicks).

All of this is deterministic signal processing — per the product spec's
MVP 1 guidance ("deterministic audio measurements and rules... more
feasible than asking one general-purpose AI model to handle every audio
task"), not machine learning.

## Optimization engine

[OptimizationEngine::buildPlan()](../src/core/OptimizationEngine.h)
turns a finished `AudioProfile` into a `FilterPlan`: concrete parameter
values for OBS's six built-in voice-processing filters, using the exact
`obs_data` setting keys verified against the real obs-studio 32.2.2
`obs-filters` plugin source:

| Spec row | OBS filter id | Driven by |
|---|---|---|
| Noise Suppression | `noise_suppress_filter` | SNR between quiet speech and the Silence noise floor (cleaner recordings get gentler suppression, to avoid damaging consonants/sounding robotic) |
| Noise Gate | `noise_gate_filter` | Silence noise floor (open threshold) vs. Quiet Speech level (safety margin) |
| Expander | `expander_filter` | Just below Quiet Speech, for gentler continuous reduction of low-level sounds |
| Compressor | `compressor_filter` | Midpoint of Normal/Loud Speech (threshold); dynamic range between Quiet and Loud Speech (ratio) |
| Limiter | `limiter_filter` | Loud Speech peak level, with a small safety margin |
| Gain | `gain_filter` | Gap between Normal Speech level and a fixed target (-18 dBFS) |

It also emits human-readable `recommendations` (the spec's "AI
recommendations" list), generated from the actual measurements rather than
canned text -- e.g. noting detected keyboard/mouse noise by keystroke/click
count, or warning when the noise floor is too close to quiet speech for
reliable gating. [recommendedFilterOrder()](../src/core/OptimizationEngine.h)
defines the signal-chain order the OBS layer applies these filters in:
Noise Suppression → Noise Gate → Expander → Compressor → Limiter → Gain.

## OBS integration

[calibration-filter.cpp](../src/obs-plugin/calibration-filter.cpp)
registers an audio filter, `AI Audio Engineer - Calibration`. While active
on a source it feeds that source's (downmixed-to-mono) audio into a
`CalibrationSession`. When calibration completes, it:

1. Runs `OptimizationEngine::buildPlan()` on the finished profile.
2. Creates/replaces six filters -- named `AI Audio Engineer - <Filter>` --
   on the **parent** source (via `obs_filter_get_parent`), in the
   `recommendedFilterOrder()` sequence, using `obs_source_create()` +
   `obs_source_filter_add()`. Re-running calibration (via "Restart Entire
   Calibration") replaces these by name, so it's safe to redo. Note this
   *adds* filters alongside any the user already has on that source rather
   than replacing them -- see the README's testing guide for the
   implication if they already have their own Compressor/Expander/etc.
3. Writes the full profile + filter plan + recommendations to the
   configured JSON path.

### Properties panel UX

Each stage requires an explicit user action before it records anything --
there is no silent auto-timing the moment the filter is attached. The
properties panel (rebuilt on every `get_properties` call, so it's just a
pure function of current state) shows:

- A **walkthrough checklist** of all 8 stages (via
  `aiaudio::allStages()`), each marked done (`[x]`), current-and-waiting
  (`[>]`), current-and-recording (`[*]`), or upcoming (`[ ]`).
- A **live status line**: "Ready: \<stage\>. Click 'Start This Stage'..."
  while waiting, a running countdown (`Xs / Ys`) for duration-based
  stages, or a **live clap counter** (`claps detected: X / 3`) for the
  Clapping stage that updates as each clap is detected.
- A **"Start This Stage" button**, wired to
  `CalibrationSession::startCurrentStage()`. `feed()` is a no-op until
  this is called (`isWaitingToStart()` / `isStageActive()`), so audio is
  never captured before the user is ready; clicking it again mid-stage
  restarts that stage's capture from zero.

Since OBS properties dialogs don't refresh on their own, `filter_audio()`
computes a small "display signature" (stage, waiting/active,
~0.2s-quantized elapsed time, live clap count) on every audio callback and
calls `obs_source_update_properties()` only when it changes -- giving a
near-live countdown/clap-counter feel (~5 updates/sec during an active
stage) without flooding the UI thread on every ~10ms audio buffer.

Build and plugin-load checks were performed with OBS Studio 32.2.2
(see "Verification performed" below).


## CalibrationSession boundary handling

[CalibrationSession](../src/core/CalibrationSession.h)
is the orchestrator: feed it mono float samples in arbitrarily sized
chunks (as a real-time audio callback would), and it automatically
segments the stream into the eight stages, exactly splitting a chunk at a
duration-based stage boundary so audio fed after a stage's time limit is
never incorrectly attributed to the stage that just ended. This matters
most for the Silence stage: without exact splitting, trailing speech from
the previous stage would otherwise contaminate the noise-floor
measurement. (Event-count-based completion, used only by Clapping, is not
split this precisely — see the class-level comment for why that's an
acceptable simplification.)

## Building and testing the core engine

The convenience script compiles the core library and all tests directly
with g++ and runs them:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

With CMake 3.16 or newer and a configured C++17 compiler, use:

```powershell
cmake -B build
cmake --build build
ctest --test-dir build
```

## Building the OBS plugin

On Windows, [tools/build-obs-plugin.ps1](../tools/build-obs-plugin.ps1)
builds against an installed 64-bit OBS Studio without requiring a full
OBS Studio development build. It requires Git and a MinGW-w64 toolchain
with `g++`, `gendef`, and `dlltool`. The script:

1. Sparse git-clones just `libobs/` and `plugins/obs-filters/` from the
   `obsproject/obs-studio` tag matching the installed OBS version (`git
   sparse-checkout`, `--filter=blob:none` — a few hundred KB, not the
   whole repo), giving exact, ABI-matching headers and the real filter
   setting keys.
2. Generates a MinGW import library (`libobs.a`) directly from the
   installed `obs.dll` via `gendef` + `dlltool` — no need to actually
   build obs-studio from source to get a linkable `obs.lib`.
3. Writes a minimal `obsconfig.h` stub (normally CMake-generated; only the
   couple of macros `obs-config.h` references are actually needed here).
4. Compiles `src/core` + `src/obs-plugin` and links
   `ai-audio-engineer.dll` fully statically (`-static`), so the only
   runtime dependencies are `obs.dll` and the OS-provided Universal CRT —
   no MinGW runtime DLLs to ship alongside it.
5. Optionally deploys it to `%ProgramData%\obs-studio\plugins\ai-audio-engineer`,
   OBS's documented per-machine third-party plugin location, which does
   **not** require administrator rights (unlike the legacy
   `Program Files\obs-studio\obs-plugins` location).

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-obs-plugin.ps1 -Deploy
```

Close OBS Studio first if it's running with a previous build of the
plugin loaded (Windows locks the DLL file while it's in use).

## Verification performed

Reported validation used OBS Studio 32.2.2 on Windows:

- The plugin compiled against libobs 32.2.2 headers and linked against an
  import library generated from `obs.dll`.
- OBS startup logs confirmed that the plugin and the built-in filter
  module loaded successfully, including a source with the calibration
  filter attached.
- All 26 core engine tests passed, including explicit-start gating,
  mid-stage restart, and live clap-count progress.

Other OBS versions and platforms have not been verified. Visual checks
of the walkthrough checklist and live clap counter, and a complete live
microphone calibration, remain pending. See the [README](../README.md)
for manual testing steps.

## Remaining work (not implemented)

- **AI Audio Quality Report** (spec section 2.D): numeric scores (voice
  clarity, noise reduction, dynamic control, overall quality) and
  before/after audio preview. This needs recording raw vs. processed audio
  and a scoring heuristic; not started.
- **Guided calibration UI**: stage instructions currently surface via the
  filter's properties panel (`OBS_TEXT_INFO`) and OBS log lines, not a
  dedicated wizard dialog (spec section 3). Building a real dialog needs
  the OBS Qt frontend API, which is a larger undertaking than the backend
  logic here.
- **ReaPlugs / third-party VST integration** (spec section 2.C, MVP 3):
  not started; the spec itself flags this needs separate validation of
  how much ReaPlugs parameters can be controlled programmatically.
- **Dedicated breath-removal mode** (spec section 2.B): breathing is
  currently only measured as its own calibration stage and folded into
  general low-level noise reduction (gate/expander), not detected and
  attenuated independently in real time, as the spec calls for.
- **Mono-only engine**: the core engine only reasons about mono audio; the
  OBS filter downmixes by averaging channels before feeding it in.
