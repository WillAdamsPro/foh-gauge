# AGENTS.md

FOH Gauge: standalone macOS app (Apple Silicon, macOS 26.0+) for FOH measurement (transfer function, RTA, delay finder).
Stack: C++ / JUCE 8 / CMake / Catch2 / GitHub Actions.

## Build and test
Core-only on Linux or macOS (no JUCE fetch; this is the PR gate in `.github/workflows/build-macos.yml`, job `linux-tests`):

    cmake -S . -B build -DFOH_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

Opt-in AddressSanitizer + UndefinedBehaviorSanitizer core/tests build (Clang or GCC; stops on the first sanitizer finding):

    cmake -S . -B build-sanitize -DFOH_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFOH_SANITIZE=address,undefined
    cmake --build build-sanitize --parallel
    ctest --test-dir build-sanitize --output-on-failure

`FOH_SANITIZE` defaults to empty; the normal build and CI gate are unchanged.

App and JUCE lifetime checks on macOS (Debug also enables JUCE's leak detector):

    cmake -S . -B build-app-sanitize -DCMAKE_BUILD_TYPE=Debug -DFOH_SANITIZE=address,undefined
    cmake --build build-app-sanitize --parallel
    ctest --test-dir build-app-sanitize --output-on-failure
    build-app-sanitize/app/fohapp_tests

The same option instruments both `FOHGauge` and `fohapp_tests`, including their JUCE module sources.
Use an unsanitized Debug app for macOS `leaks --atExit`; quit normally so JUCE shutdown runs.

Opt-in ThreadSanitizer build (Clang or GCC; separate from ASan/UBSan):

    cmake -S . -B build-thread -DFOH_BUILD_APP=OFF -DFOH_TSAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build-thread --parallel
    TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-thread --output-on-failure

For the macOS app and both core/app test suites, use a separate directory:

    cmake -S . -B build-thread-app -DFOH_BUILD_APP=ON -DFOH_TSAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
    cmake --build build-thread-app --parallel
    TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-thread-app --output-on-failure

`FOH_TSAN` defaults to OFF and cannot be combined with `FOH_SANITIZE`.
It instruments the core, app and app tests (including their JUCE modules).
The synthetic `[concurrent]` app tests exercise live sample-rate restarts,
capture reset/pause/resume, and settings/calibration changes without hardware.
Audio callbacks use only atomics and SPSC buffers for cross-thread communication;
device lifecycle callbacks must remain serialized with I/O as required by JUCE.

macOS app (JUCE fetched by CMake; needs Xcode 26):

    cmake -S . -B build-app -G Xcode -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
    cmake --build build-app --config Release

The macOS CI job runs only on pushes to master, `v*` tags and workflow_dispatch. Releases (`release.yml`) build an unsigned zipped .app + DMG on `v*` tags, as prereleases.

## Layout
- `core/`: DSP (TF, RTA, delay finder, calibration, settle/coherence). Owned by Orion.
- `tests/`: Catch2 tests and fixtures (`tests/data/`, synthetic only). Owned by Orion.
- `app/`: JUCE UI (panels, workspaces, themes).
- `.github/workflows/`: CI and release.

## Rules
- Repo is public but low-profile: no LICENSE, description, topics or badges; minimal README; never mention a church or venue.
- Never commit secrets, tokens, real calibration files or venue data. Test fixtures must be synthetic (e.g. `tests/data/synthetic-mic-cal.csv`).
- Never push or force-push master.

## PR workflow
Branch from master (`area/short-name`), keep PRs small and self-contained, run the core tests locally, open a PR to master, and merge with a merge commit only when CI is green. Stacked PRs get retargeted to master once the base merges.
