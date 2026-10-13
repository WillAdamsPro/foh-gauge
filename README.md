# FOH Gauge

Guided FOH measurement tool (transfer function, delay finder, RTA, sub/fill alignment guides).
macOS Apple Silicon standalone app, JUCE 8 + CMake. DSP lives in `core/` (JUCE-free, Catch2-tested).

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release      # core DSP tests
```
`-DFOH_BUILD_APP=OFF` builds only the core + tests (Linux CI). Linux needs the usual JUCE deps (ALSA, X11, freetype) to build the app as a compile check.

## Download (macOS)
Each push builds an **unsigned** arm64 app in GitHub Actions → *build-macos* run → artifact `FOH-Gauge-macOS-arm64`.
Because it's unsigned, macOS Gatekeeper will block it the first time. Either:
- right-click (Control-click) `FOH Gauge.app` → **Open** → Open, or
- `xattr -dr com.apple.quarantine "/Applications/FOH Gauge.app"`

The app asks for microphone permission on first launch (needed for audio input).

## Default routing (Behringer UMC404HD)
- Out 1 → console / system input (generator)
- Out 2 → **1/4" TRS** cable → In 2 = reference loopback (TRS keeps it off the +48 V XLR pins)
- In 1 = Behringer ECM8000 (XLR, +48 V)

## Mic calibration
Load CSV/TXT/FRD files: `Hz,dB[,phase]`, comma/tab/space/semicolon separated; headers, `*`/`#`/`;` comments and a `Sens Factor` line are tolerated.

## Layout
- `core/` – FFT, pink noise/log sweep, Welch dual-channel TF (mag/phase/coherence), smoothing, delay finder (xcorr / GCC-PHAT, top 3 arrivals), RTA (1/3, 1/6 oct), mic cal, trace store (8), guide helpers.
- `tests/` – Catch2 tests on synthetic signals.
- `app/` – JUCE GUI.
