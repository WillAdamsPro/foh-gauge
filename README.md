# FOH Gauge

Guided FOH measurement tool (transfer function, delay finder, RTA, sub/fill alignment guides).
Standalone app for macOS Apple Silicon (macOS 26.0+) and Windows x64, JUCE 8 + CMake. DSP lives in `core/` (JUCE-free, Catch2-tested).

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release      # core DSP tests
```
`-DFOH_BUILD_APP=OFF` builds only the core + tests (Linux CI). Linux needs the usual JUCE deps (ALSA, X11, freetype) to build the app as a compile check.

## Windows build
Use Visual Studio 2022 with the C++ desktop workload, CMake and Python with NumPy (`python -m pip install -r tools/requirements-golden.txt`).
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
Windows CI uploads `FOH-Gauge-Windows-x64`; extract and run `FOH Gauge.exe`.
WASAPI shared/exclusive modes work without an SDK. Select the audio system, devices and channels in Audio Setup.
ASIO is optional: install your interface's ASIO driver and obtain the Steinberg ASIO SDK outside the repo, then configure with `-DFOH_ENABLE_ASIO=ON -DFOH_ASIO_SDK_DIR=C:/path/to/asiosdk` (the root containing `common/asio.h` and `common/iasiodrv.h`). ASIO uses the same interface for input and output; changing either selection updates both. The SDK is never fetched or distributed by CI.

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
