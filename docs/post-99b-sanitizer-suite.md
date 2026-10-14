# Post-99b sanitizer suite rerun

Base: fetched master `824b944c60952d0e330c13da73b88d2675d3be5f`
(including hardening PRs #136–#140 and preference recovery #131).
Local environment: Apple Silicon arm64, macOS 26.2, AppleClang 21 Command
Line Tools, JUCE 8.0.4, deployment target 26.0. Full Xcode is not installed;
the local app builds use Unix Makefiles.

## Reproduced finding

The full TSan suite crashed at process teardown after the W7 monitoring stress
test passed its 2,208 assertions. The native stack ended in `objc_msgSend`,
`_NSWindowTransformAnimation _stopAnimation:withDisplayLink:` and a background
display-link callback. The test had created its own desktop peer with
`panel->addToDesktop(0)`, bypassing the native animation safeguard already present
in the app's `FloatingPanelWindow`.

Visible panel fixtures now use that production window owner, with the window
destroyed before its non-owned panel and backing layout state. This also covers
the visible TF, stored-trace and saved-target fixtures, and the optional W7 CPU
fixture. The requested panel content dimensions are preserved. There is no
product behavior or audio-thread change.

The W7 Catch2 regression verifies `NSWindowAnimationBehaviorNone`, then repeats
24 immediate show/destroy cycles with monitoring active, drains messages and
checks that monitoring and desktop peer counts return to their original state.
The native setting check failed against the original bare peer (`0 == 2`,
2,210/2,211 assertions passed). The expanded regression passes all 2,308
assertions under both ASan/UBSan and TSan. Only this test's native checks require
the additional Objective-C++ compile mode on macOS; other platforms retain C++.

An initial ASan CTest run also hit the existing three-second wait for a
mic-position/delay result while multiple build/test configurations overlapped.
The same test passed all 145 assertions in isolation without changes. Its
deadline and assertions remain unchanged.

The unoptimized Debug core's 25-hour Leq regression reached CTest's default
1,500-second timeout without a sanitizer finding. It processes 4.32 billion
samples; a process sample showed active filter processing and a 22 MB peak
footprint. The same test passes in Release, core RelWithDebInfo ASan/UBSan and
TSan. The final Debug sanitizer rerun adds `-O1` to the base Debug flags so the
core completes within the existing deadline. JUCE's target flags still append
`-O0` for FOHGauge and fohapp_tests, including their JUCE modules. Debug
assertions and leak detection remain enabled. No test deadline, assertion or
sanitizer setting is relaxed.

## Reproduction

Configure and build the six AGENTS.md configurations: Release core (`build`),
RelWithDebInfo core ASan/UBSan (`build-sanitize`), core TSan (`build-thread`),
Release app (`build-app`), Debug app ASan/UBSan (`build-app-sanitize`), and
RelWithDebInfo app TSan (`build-thread-app`). Both app and app-test executables
are built. Sanitizers remain separate, without suppressions or excluded cases.
For the final Debug app sanitizer configuration on this Mac, use:

```sh
cmake -S . -B build-app-sanitize -DFOH_BUILD_APP=ON \
  -DCMAKE_BUILD_TYPE=Debug -DFOH_SANITIZE=address,undefined -DFOH_TSAN=OFF \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  '-DCMAKE_CXX_FLAGS_DEBUG=-g -O1' '-DCMAKE_C_FLAGS_DEBUG=-g -O1' \
  '-DCMAKE_OBJCXX_FLAGS_DEBUG=-g -O1'
cmake --build build-app-sanitize --parallel 6
```

```sh
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-app-sanitize --output-on-failure -j 4
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build-app-sanitize/app/fohapp_tests
TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-thread-app --output-on-failure -j 4
TSAN_OPTIONS=halt_on_error=1 build-thread-app/app/fohapp_tests
```

Inspect successful-test logs as well as failures: JUCE Debug assertions can log
without causing a test failure. Debug retains JUCE's leak detector, and the
sanitizer flags cover the app and app tests' JUCE C/C++/Objective-C++ modules.

## Results

All six configurations build successfully. Final CTest results:

| Configuration | Passed / total |
| --- | --- |
| Release core gate | 189 / 189 |
| RelWithDebInfo core ASan/UBSan | 189 / 189 |
| RelWithDebInfo core TSan | 189 / 189 |
| Release app, including core/layout/console/golden | 339 / 339 |
| Debug app ASan/UBSan, core at `-O1`, app/JUCE at `-O0` | 339 / 339 |
| RelWithDebInfo app TSan, including core/layout/console/golden | 339 / 339 |

The final Debug sanitizer full suite completes in 104 seconds, including the
25-hour Leq regression. Before the core flag adjustment, the corrected Debug
app-only CTest suite also passed 150/150 cases with the core at `-O0`.
Single-process app runs under both sanitizers pass 150 cases / 553,807
assertions, including teardown between cases and process exit. Successful-test
CTest logs and direct app logs contain no sanitizer findings, JUCE assertions
or leaked-object warnings. Both sanitizer configurations retain all cases.
The arm64 Release app has `LSMinimumSystemVersion` 26.0. Public string and
calibration guards, their synthetic planted-input regressions, and
`git diff --check` pass.

Synthetic tests do not establish physical-device hot-unplug, microphone consent
or multiple-display removal behavior. This pass does not repeat the separate
system heap-leak investigation in `app-lifetime-audit.md`, or enable the optional
`FOH_W7_CPU` real-time benchmark.
