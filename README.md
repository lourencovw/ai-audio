# AI Audio Engineer for OBS Studio

An intelligent OBS Studio plugin that guides a user through a short
microphone calibration routine, analyzes the recordings, and automatically
configures OBS's audio filters for professional, studio-quality voice
audio. See the original product concept for the full vision, feature set,
and three-phase MVP roadmap.

## Current status: MVP 1 implemented

- **Calibration engine**: the eight-stage guided calibration sequence
  (normal/loud/quiet speech, silence, clapping, keyboard, mouse,
  breathing), each measuring RMS/peak/noise-floor levels, a Welch-averaged
  frequency fingerprint, and transient-event counts.
- **Optimization engine**: deterministically turns those measurements into
  concrete settings for OBS's six built-in voice filters (Noise
  Suppression, Noise Gate, Expander, Compressor, Limiter, Gain), plus
  human-readable recommendations.
- **OBS plugin**: an audio filter, `AI Audio Engineer - Calibration`, with
  a built-in guided walkthrough in its properties panel — a checklist of
  all 8 stages, an explicit **"Start This Stage"** button per stage (no
  stage times anything until you click it), and live progress (countdown
  or a live clap counter). It automatically adds/configures the six
  filters on the source once calibration finishes.
- **Compatibility**: Windows build scripts are provided. Build and plugin-load
  checks were performed with OBS Studio 32.2.2; other versions and platforms
  have not been verified. Full interactive calibration validation remains
  pending (see the design notes).

**Not yet implemented** (see [docs/CALIBRATION_ENGINE.md](docs/CALIBRATION_ENGINE.md)
"Remaining work" for details): the AI Audio Quality Report with
before/after audio preview, a standalone wizard dialog outside the
Filters properties panel, ReaPlugs integration, and a dedicated real-time
breath-removal mode.

## Project layout

```
src/core/                  Calibration + optimization engine — pure C++17, no OBS dependency
src/obs-plugin/            OBS Studio audio-filter integration
tests/                     26 unit tests for the core engine (custom lightweight framework)
tools/build.ps1            Compiles + runs the core engine and tests with g++ directly
tools/build-obs-plugin.ps1 Builds (and optionally deploys) the real OBS plugin DLL
CMakeLists.txt             CMake build (core lib + tests always; OBS plugin opt-in)
docs/                      Design notes, verification performed, and remaining work
```

## Build prerequisites

- A C++17 compiler for the core engine (g++ for the PowerShell scripts).
- Windows and PowerShell for the provided build scripts.
- Git, an installed 64-bit OBS Studio, and a MinGW-w64 toolchain with
  `g++`, `gendef`, and `dlltool` for the plugin build.
- CMake 3.16 or newer for the optional CMake build.

## Command shortcuts

Run these commands from the project root in PowerShell or Command Prompt:

| Command | Action |
|---|---|
| `.\run` | Show available commands |
| `.\run build` | Compile the core engine and tests |
| `.\run test` | Compile and run all core tests |
| `.\run plugin` | Build the OBS plugin |
| `.\run deploy` | Build and install the plugin; close OBS first |
| `.\run clean` | Delete `build/`, including downloaded OBS headers; the next plugin build downloads them again |

For a custom OBS installation, use
`.\run plugin -ObsInstallDir "D:\Apps\obs-studio"` (also supported by `deploy`).
These shortcuts use the PowerShell build scripts and do not require CMake.

## Building & testing the core engine

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

Compiles `src/core` and `tests` with g++ (MinGW-w64) and runs the test
binary. All 26 unit tests currently pass.

## Building the OBS plugin

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-obs-plugin.ps1 -Deploy
```

This fetches matching libobs headers, generates an import library from
your installed `obs.dll`, builds `ai-audio-engineer.dll`, and deploys it
to `%ProgramData%\obs-studio\plugins\ai-audio-engineer` (no admin rights
needed). Close OBS first if it's currently running with the plugin
loaded. See [docs/CALIBRATION_ENGINE.md](docs/CALIBRATION_ENGINE.md)
for exactly how this script works and what was verified.

## How to test it in OBS Studio

1. **Build and deploy** the plugin (see above), then **start OBS Studio**.
   On startup, confirm it loaded: open **Help → Log Files → View Current
   Log** (or check `%APPDATA%\obs-studio\logs\` for the newest file) and
   look for:
   ```
   [ai-audio-engineer] plugin loaded (MVP1: calibration engine)
   ```
   If you don't see it, the plugin didn't load — check for an error on the
   same line instead.

2. **Add the filter to your microphone.** In the **Audio Mixer** dock,
   click the gear/menu icon on your mic's channel strip (or right-click
   it) → **Filters**. In the Filters dialog, click the **+** under
   "Audio/Video Filters" and choose **AI Audio Engineer - Calibration**.
   (You can also add it as a filter on a scene's audio-producing source
   instead of the global Mic/Aux channel, the same way.)
   > **Heads up:** if your mic already has its own manually-configured
   > Compressor/Expander/Noise Suppression/EQ filters, calibration will
   > **add** the six AI Audio Engineer filters alongside them rather than
   > replacing them — which can double up processing (e.g. two
   > compressors in a row). Consider temporarily removing your existing
   > ones before calibrating, or removing the AI Audio Engineer ones
   > afterward if you'd rather keep your own.

3. **Run the calibration, one stage at a time.** With the filter
   selected, its properties panel shows:
   - A **walkthrough checklist** of all 8 stages (`[x]` done, `[>]`
     current/ready, `[*]` currently recording, `[ ]` upcoming), so you can
     see the whole flow and your progress at a glance.
   - A **status line** for the current stage and its instruction (e.g.
     *"Ready: Normal speech. Click 'Start This Stage' below when you're
     ready."*).
   - A **"Start This Stage" button** — nothing is recorded until you
     click it. Get into position, then click it and perform that stage's
     action (speak, stay silent, clap, type, etc.). If you weren't ready
     or want a do-over, just click it again to restart that stage from
     zero.
   - While recording, the status line live-updates: a countdown
     (*"Recording \"Normal speech\" -- 4.2s / 10.0s"*) for timed stages,
     or a **live clap counter** for the Clapping stage (*"claps detected:
     2 / 3"*) that increments as each clap is detected.
   - The stage advances automatically once its timer (or 3 claps) completes
     — then click **Start This Stage** again for the next one. Repeat for
     all 8 stages (~10s each, a few seconds for claps).

4. **Check the result.** Once the walkthrough shows all stages as `[x]`
   and the status line says *"Calibration complete..."*, re-open the
   Filters dialog for that source: you should now see six new filters —
   *AI Audio Engineer - Noise Suppression / Noise Gate / Expander /
   Compressor / Limiter / Gain* — already configured and listed in that
   processing order. The OBS log also prints the generated
   recommendations (e.g. detected keyboard noise, the gain adjustment
   applied, the compressor ratio chosen).

5. **Inspect the full report.** The filter's "Save profile + filter plan
   to" property controls where a JSON file with the complete measurements,
   filter settings, and recommendations gets written (defaults to
   `ai-audio-profile.json` in OBS's working directory). Open it after
   calibration completes to see everything the engine measured and
   decided.

6. **Redo it anytime.** Click **Restart Entire Calibration** in the
   filter's properties to go back to stage 1 and run through all 8 stages
   again — it safely replaces the previous six filters rather than
   duplicating them.

If anything in the Filters dialog looks off (missing filter type, wrong
values, etc.), check the OBS log for warnings from
`[ai-audio-engineer]` first — that's the fastest way to see what the
plugin actually did.

