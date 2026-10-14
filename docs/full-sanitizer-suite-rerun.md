# Full sanitizer suite rerun

Base: master `39b02c8`. Local environment: Apple Silicon (arm64), macOS 26.2,
AppleClang 21 Command Line Tools, JUCE 8.0.4, deployment target 26.0.
Xcode is not installed; app builds use Unix Makefiles.

The direct Debug ASan/UBSan app suite exposed a JUCE assertion in
`juce_String.cpp:327`: console UI literals containing UTF-8 em dashes went
through JUCE's ASCII `const char*` constructor. This also corrupted the displayed
text. Decode all four affected menu/status strings explicitly with
`String::fromUTF8`, using escaped UTF-8 bytes.

The hardware-free Catch2 regression checks the desk menu and a synchronous
selection-change status. Against the original implementation, both text checks
failed and both paths logged the JUCE assertion. With the fix, all four assertions
pass without JUCE diagnostics. The test opens no socket and sends no console
messages. No audio-thread code or sanitizer configuration changed.

## Reproduction

Use the AGENTS.md build commands for:

- Core Release (`build`, app off).
- Core ASan/UBSan (`build-sanitize`, RelWithDebInfo, app off).
- Core TSan (`build-thread`, RelWithDebInfo, app off).
- App ASan/UBSan (`build-app-sanitize`, Debug).
- App TSan (`build-thread-app`, RelWithDebInfo).
- App Release (`build-app`, Unix Makefiles on this Mac).

All app configurations build both FOHGauge and fohapp_tests. Verified sanitizer
flags on the core, app and app-test targets, including C/C++/Objective-C++ JUCE
module sources. Sanitizers run separately without suppressions or excluded cases:

```sh
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-app-sanitize --output-on-failure -j 4
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build-app-sanitize/app/fohapp_tests
TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-thread-app --output-on-failure -j 4
TSAN_OPTIONS=halt_on_error=1 build-thread-app/app/fohapp_tests
```

Inspect captured successful-test output as well as failure output: JUCE Debug
assertions can log without making Catch2 or CTest fail.

## Results

All builds succeeded. Final CTest results include the UTF-8 regression:

| Configuration | Passed / total |
| --- | --- |
| Core Release | 163 / 163 |
| Core ASan/UBSan | 163 / 163 |
| Core TSan | 163 / 163 |
| App Release, including core/layout/console/golden | 258 / 258 |
| App Debug ASan/UBSan, including core/layout/console/golden | 258 / 258 |
| App TSan, including core/layout/console/golden | 258 / 258 |

- Direct app suites: 95 cases / 530,438 assertions passed under each sanitizer.
- Direct core suites: 132 cases / 946,953 assertions passed under each sanitizer.
- Direct layout suites: 21 cases / 1,195 assertions passed under each sanitizer.
- Direct console protocol suites: 9 cases / 3,534 assertions passed under each
  sanitizer. Golden tests run as a single process through CTest.
- The final ASan Debug app launched for five seconds with
  `FOH_WORKSPACE=3 FOH_FLOAT=RTA`, then received a normal AppleEvent quit request
  addressed to its exact PID. Exit 0; no ASan/UBSan or JUCE diagnostics.
  There is no dedicated headless app mode; this was a scripted desktop launch
  without user interaction.
- Final CTest captured output and direct-process logs contain no sanitizer
  findings, JUCE assertions or leaked-object warnings. Debug checks retain
  JUCE's leak detector. No sanitizer suppressions or skipped tests were added.
- Public string scan and `git diff --check` passed.

## Remaining limitations

The additional unsanitized Debug `leaks --atExit` check produced no heap report.
After the normal quit request, the tool stalled with its app child suspended;
attempts timed out after 45 and 180 seconds. This is inconclusive, not a clean
system heap-leak result. No probe processes remain.

The earlier AppKit window-tiling-controller finding (10 allocations / 560 bytes)
in [app-lifetime-audit.md](app-lifetime-audit.md) remains deferred and unverified
by this rerun. Changing AppKit/JUCE teardown without a new actionable report is
outside this focused fix. No sanitizer errors or races remain from this pass.

Synthetic callbacks and a brief launch/quit do not establish physical-device
hot-unplug, microphone-consent or multiple-display removal behavior.
