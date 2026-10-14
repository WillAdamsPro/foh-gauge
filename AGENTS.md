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
