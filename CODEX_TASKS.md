# CODEX_TASKS.md: prioritized, self-contained coding tasks
Read AGENTS.md first. Every task: branch from master, one PR, merge-commit on green CI.
Default test command (T): `cmake -S . -B build -DFOH_BUILD_APP=OFF && cmake --build build -j && ctest --test-dir build --output-on-failure`
App tasks also require the macOS build (dispatch build-macos on master after merge, or build locally on a Mac).

## P0: build 0 (Thu Oct 16), integrator only
0. Merge queue #16-#18, #27-#30, #31-#37, #40, #45, #26, #38; build-macos green with LSMinimumSystemVersion 26.0; test tag, then v0.0 prerelease.

## P1: app (UI queue)
1. Replace `app/**/QaAdapters.h` with core APIs. Files: QaAdapters.h (delete), its includers in app/. Use classifyCoherence (>=0.9 green, 0.6-0.9 amber, <0.6 or NaN red), setConvergenceTimeout(10 s), subDelayFromPhase, double reference delay, DelayResult::ok(). Accept: no QaAdapters.h, app builds, T passes.
2. MainComponent fixes. Files: app/MainComponent.*. Clear the stereo history ring on sample-rate change; run findDelay async via CommandQueue (never on the audio thread); use fetch_max for micPeak; keep 3 s of sub history. Accept: macOS build green; no allocations or locks on the audio thread.
3. Ctrl+Tab / Ctrl+Shift+Tab cycles workspaces. Files: app workspace switcher. Accept: wraps around, matches Cmd+1-4 order.
4. Show mode: minimal RTA/Show view hiding the inspector and helper. Accept: toggle persists across relaunch.
5. Venue-mention scan: add `scripts/check-strings.sh` run in linux CI, grepping app/, README and resources for venue/church terms (wordlist in the script). Accept: CI fails on a planted term.

## P2: packaging and CI
6. #38 release workflow: create releases with `prerelease: true` and plain notes; attach zip, DMG and SHA256SUMS. Accept: a test tag produces a prerelease with all three assets.
7. #26 macOS 26.0 target: resolve conflict with merged #42 (keep #42's triggers/concurrency, take #26's deployment target and runner).
8. Block real cal files: .gitignore `*.cal`, `*.frd`, `*-cal.csv` except `tests/data/synthetic-*`; CI step failing on any tracked match. Accept: CI fails on a planted `foo-cal.csv`.
9. JUCE fallback: if Xcode 26 breaks JUCE 8.0.4, branch `ci/juce-bump` pinning 8.0.15 in CMakeLists.txt. Accept: build-macos green.

## P3: tests (simulated room)
10. #43 review fixes: rename rounding helper to "half away from zero", document it as message-thread only, move it to core format.h/.cpp, add edge tests (x.xx5, negatives, NaN/inf).
11. After #45 merges: drop the xfails in #43/#44 that #45 fixes.
    - Deferred on master: #45 is merged (`4341c11`), but #43 and #44 remain open drafts; their `test_part_a.cpp` and `test_room_sim.cpp` tests and `[!shouldfail]` markers are not present here. Do not import the stacked drafts just to remove their xfails.
    - Once those tests land, remove the fixed D-SGN and S16 reflection-level xfails. D-SGN tests must explicitly enable negative-lag search with `DelayOptions::minDelayMs < 0` (or symmetric search); the default still searches positive lags only. Update the Part A coverage map to reflect passing tests and run T. Master's existing S16 rate-independence and D-SGN regression tests already assert #45's fixes without xfails.
12. Rebase #39/#41/#43/#44 onto master using `tests/data/synthetic-mic-cal.csv` (#46); update expected values. Accept: T passes.

## Build #1 (Oct 25, hard window Oct 25-27)
13. Workspace presets Setup / Alignment / Tuning / RTA-Show with Cmd+1-4. Accept: each preset restores its panel set.
14. Tear-off floating panel windows with per-display restore and fallback to the main display when the saved display is missing. Accept: relaunch restores positions; unplugging a display does not lose windows.
15. Confirm settle detector, traffic light and two-spike flag are wired in UI per the QA decisions (settle <0.5 dB RMS x3 with >=25% coherent bins, 10 s timeout warning; two-spike within 3 dB and >0.5 ms apart).
Not in build #1: export, SPL cal, live delay tracker, user layouts, drag-to-dock, Windows.
