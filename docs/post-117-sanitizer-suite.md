# Post-117 sanitizer suite rerun

Base: fetched master `82d8eae5db6486794a68da5583e3788c96a90fc8`, including
the UI polish and hardening merges through #176/#177. Environment: Apple
Silicon arm64, macOS 26.2, AppleClang 21 Command Line Tools, SDK 26.5,
JUCE 8.0.4, deployment target 26.0. Full Xcode is unavailable; local app
builds use Unix Makefiles.

## Reproduced findings

The initial Debug ASan/UBSan app suite failed the toolbar regression's
theme-colour checks and logged JUCE's `juce_Component.cpp:2694` assertion
about focusing a component before it is showing. A separate run of that
test reproduced both diagnostics.

Setup can be prepared during first-launch construction before the main
window is shown. Its section and SPL-calibration helpers now scroll and
prepare their content as before, but request keyboard focus only when
the target is showing. A synthetic regression exercises all five sections
and SPL calibration under a parent without a desktop peer, checking the
selected section and preservation of keyboard focus. The Debug logs also
check for the otherwise nonfatal JUCE assertions on this path.

The toolbar test previously assumed a 30 ms message loop would deliver the
asynchronous theme-change notification. Instrumented builds can exhaust
that interval before delivery. The test now dispatches the pending theme
notification explicitly before checking colours. The focused test passes
all 202 assertions without JUCE assertions after the changes.

Inspection of successful-test logs also found the same focus assertion in
headless workspace tab activation: when the replacement tab was not showing,
the fallback unconditionally focused its hidden overflow button. The fallback
now checks visibility too. Existing workspace stress and Expert traversal
regressions exercise that branch. The count-stepper text-edit regression
also opened a JUCE editor without a visible parent; it now uses the existing
nonanimated floating-window fixture for the actual editing interaction.
No production theme behaviour or audio-thread code changed.

Running sanitizer configurations concurrently also exposed a temporary-folder
collision in the Save As/Open audio regression. Its unused-name lookup did
not reserve the directory, so separate test processes could share it and
delete each other's session files. That fixture now uses a UUID-named folder.
The existing six generated Save As/Open scenarios cover the same path.

## Reproduction

Build the six configurations from AGENTS.md: Release core (`build`),
RelWithDebInfo core ASan/UBSan (`build-sanitize`), core TSan (`build-thread`),
Release app (`build-app`), Debug app ASan/UBSan (`build-app-sanitize`) and
RelWithDebInfo app TSan (`build-thread-app`). Both app targets are built in
every app configuration. The core ASan/UBSan configuration also enables
`FOH_ENABLE_CONSOLE_CONTROL=ON` to cover the optional SQ protocol tests;
the other configurations use the default OFF.

As in the post-107 audit, Debug sanitizer base flags use `-g -O1` for C,
C++ and Objective-C++ so the core's 25-hour synthetic Leq test fits CTest's
deadline. JUCE app/test targets append `-O0`; Debug assertions and JUCE leak
detection remain enabled.

```sh
cmake -S . -B build-app-sanitize -DFOH_BUILD_APP=ON \
  -DCMAKE_BUILD_TYPE=Debug -DFOH_SANITIZE=address,undefined -DFOH_TSAN=OFF \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  '-DCMAKE_CXX_FLAGS_DEBUG=-g -O1' '-DCMAKE_C_FLAGS_DEBUG=-g -O1' \
  '-DCMAKE_OBJCXX_FLAGS_DEBUG=-g -O1'
cmake --build build-app-sanitize --parallel 6
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-app-sanitize --output-on-failure -j 4
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build-app-sanitize/app/fohapp_tests
TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-thread-app --output-on-failure -j 4
TSAN_OPTIONS=halt_on_error=1 build-thread-app/app/fohapp_tests
```

Inspect CTest's `Testing/Temporary/LastTest.log` as well as direct-process
logs: JUCE assertions can log without failing Catch2. Verify sanitizer flags
on core, app and app-test sources, including JUCE's C/C++/Objective-C++
modules. No suppressions or excluded tests are used.

## Results

All six configurations build successfully. Final CTest results:

| Configuration | Passed / total | Seconds |
| --- | --- | --- |
| Release core gate | 212 / 212 | 21.13 |
| RelWithDebInfo core ASan/UBSan, optional SQ protocols enabled | 223 / 223 | 104.20 |
| RelWithDebInfo core TSan | 212 / 212 | 217.94 |
| Release app, including core/layout/console/golden | 426 / 426 | 43.96 |
| Debug app ASan/UBSan, core at `-O1`, app/JUCE at `-O0` | 426 / 426 | 129.52 |
| RelWithDebInfo app TSan, including core/layout/console/golden | 426 / 426 | 355.19 |

The focused Debug ASan/UBSan run of the five affected focus/toolbar cases
passes all 5,317 assertions without JUCE assertions. The Release app is an
arm64 executable with `LSMinimumSystemVersion` 26.0. Public string,
calibration-file and UI-token guards, PR-gate scan regressions and
`git diff --check` pass.

Single-process app runs under both ASan/UBSan and TSan pass all 214 cases
and 3,390,142 assertions each, including teardown between cases and process
exit. All six final CTest captured-output logs and both direct logs contain
no sanitizer reports, JUCE assertions or leaked-object warnings. No crash,
use-after-free, data race or undefined behaviour was reproduced in the final
runs. No unresolved reproducible finding remains from this pass.

Synthetic tests do not establish physical-device unplug/replug, microphone
consent or physical multiple-display removal behaviour. This pass does not
repeat the separate system heap-leak investigation or enable the optional
`FOH_W7_CPU` real-time benchmark.
