# Post-107 sanitizer suite rerun

Base: fetched master `4b1b2c7d30c550c22c2ebfac7ed4e41cdfd3f271`, after the
tasks 101–107 hardening batch. Local environment: Apple Silicon arm64,
macOS 26.2, AppleClang 21 Command Line Tools, SDK 26.5, JUCE 8.0.4,
deployment target 26.0. Full Xcode is unavailable; app builds use Unix Makefiles.

## Reproduced finding

The Debug ASan/UBSan app suite logged a JUCE assertion at
`juce_String.cpp:327` when constructing the acknowledgements screen. Its
heading passed UTF-8 bytes for an em dash into the ASCII `String(const char*)`
constructor, corrupting the heading even in Release.

The heading now uses `String::fromUTF8` with explicit UTF-8 byte escapes.
The existing Catch2 reader test checks the decoded heading. That assertion
failed against the original implementation in both Release and Debug
ASan/UBSan (three assertions passed, one failed).
No audio-thread code or product features changed.

## Reproduction

Configure/build all six AGENTS.md configurations: Release core (`build`),
RelWithDebInfo core ASan/UBSan (`build-sanitize`), core TSan (`build-thread`),
Release app (`build-app`), Debug app ASan/UBSan (`build-app-sanitize`), and
RelWithDebInfo app TSan (`build-thread-app`). App configurations build both
FOHGauge and fohapp_tests.

The Debug sanitizer configuration retains the previous audit's base Debug
flags of `-g -O1` for C, C++ and Objective-C++. This allows the core's
25-hour synthetic Leq test to finish within CTest's existing deadline.
JUCE app/test targets append `-O0`; Debug assertions and JUCE leak detection
remain enabled. No test is skipped and no sanitizer suppression is used.

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

Inspect successful-test CTest logs and direct-process logs for sanitizer
reports, JUCE assertions and leaked-object warnings, since JUCE assertions
can log without failing Catch2. Sanitizer compile flags were verified on
the core, app and app tests, including JUCE C/C++/Objective-C++ modules.

## Results

All six configurations build successfully. Final CTest results:

| Configuration | Passed / total | Seconds |
| --- | --- | --- |
| Release core gate | 203 / 203 | 43.92 |
| RelWithDebInfo core ASan/UBSan | 203 / 203 | 136.73 |
| RelWithDebInfo core TSan | 203 / 203 | 585.69 |
| Release app, including core/layout/console/golden | 369 / 369 | 32.55 |
| Debug app ASan/UBSan, core at `-O1`, app/JUCE at `-O0` | 369 / 369 | 160.29 |
| RelWithDebInfo app TSan, including core/layout/console/golden | 369 / 369 | 346.77 |

Single-process app runs under both ASan/UBSan and TSan pass all 166 cases
and 3,272,699 assertions, including teardown between cases and process exit.
All six final CTest captured-output logs and both direct app logs contain no
sanitizer reports, JUCE assertions or leaked-object warnings. No crash,
use-after-free, data race or undefined behavior was reproduced in the final
runs. No unresolved reproducible finding remains from this pass.

The arm64 Release app has `LSMinimumSystemVersion` 26.0. Public string and
calibration-file guards, their existing regressions, license guards and
`git diff --check` pass.

Synthetic tests do not establish physical-device hot-unplug, microphone
consent or multiple-display removal behavior. This pass does not repeat
the separate system heap-leak investigation or enable the optional
`FOH_W7_CPU` real-time benchmark.
