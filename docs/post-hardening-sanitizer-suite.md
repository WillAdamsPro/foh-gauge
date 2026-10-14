# Post-hardening sanitizer suite

Base: master `64aa203`, after the recent audio, file/session and guide hardening
merges. Local environment: Apple Silicon (arm64), macOS 26.2, AppleClang 21
Command Line Tools, JUCE 8.0.4, deployment target 26.0. Xcode is not installed;
local app builds use Unix Makefiles.

## Finding and regression

The initial full TSan run crashed after the console-window ownership test,
four session-notice tests and a target-notice test had passed their assertions.
The native stacks ended in `objc_msgSend`, AppKit's
`_NSWindowTransformAnimation _stopAnimation:withDisplayLink:`, and a background
display-link callback. An automatic visibility animation outlived the deleted
JUCE window peer, matching the previously fixed floating-window failure.

Set the console window's public `NSWindow.animationBehavior` to
`NSWindowAnimationBehaviorNone` before its first show, as for floating panels.
Apply the same setting to JUCE alerts created by the app's look-and-feel before
modal display, retaining the base factory's button and callback behavior.
Compile the affected implementation and native regressions as Objective-C++ on
macOS; other platforms retain C++ and the existing visibility behavior. No
audio-thread code changes.

The new hardware-free Catch2 regression checks native animation behavior and
repeats 24 immediate close/reopen/destroy cycles, draining messages after each
destruction and checking desktop peer counts. It opens no network connection
and sends no console messages. Before the fix, all 24 native behavior checks
failed (120/144 assertions passed). After the fix, all 144 assertions passed.
The session-notice regression replaces a missing synthetic session notice eight
times, checks native animation behavior, then destroys the owner while the final
notice is modal. All eight native checks failed before the fix (42/50 assertions
passed); all 50 passed afterward. Both regressions also passed together under
TSan (194 assertions).

The standalone target-panel test now installs the same default look-and-feel as
the app shell. It previously exercised JUCE's default alert factory instead of
the app factory. It explicitly observes and dismisses all six rejected-target
notices, checking native animation behavior before panel teardown (62 assertions).

## Verification

ASan/UBSan and TSan use separate AGENTS.md build directories, with the full core,
layout, console, golden and app suites enabled. Debug ASan/UBSan retains JUCE's
assertions and leak detector. Both FOHGauge and fohapp_tests, including their
C/C++/Objective-C++ JUCE module sources, have the corresponding sanitizer flags.
No suppressions, excluded cases or relaxed assertions were added.

Reproduction (build commands are in AGENTS.md):

```sh
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-app-sanitize --output-on-failure -j 4
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build-app-sanitize/app/fohapp_tests
TSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-thread-app --output-on-failure -j 4
TSAN_OPTIONS=halt_on_error=1 build-thread-app/app/fohapp_tests
```

Final full CTest reruns:

| Configuration | Passed / total |
| --- | --- |
| Normal Release core gate (`build`, app off) | 178 / 178 |
| Debug ASan/UBSan, core and app | 307 / 307 |
| RelWithDebInfo TSan, core and app | 307 / 307 |

The sanitizer runs include room-sim and generated golden signals. The app and
test executables also build successfully in unsanitized Release. The app's
`LSMinimumSystemVersion` is 26.0. Successful-test CTest logs contain no sanitizer
findings, JUCE assertions or leaked-object warnings. Public string/calibration
guards and `git diff --check` pass.

Direct app-suite runs under each sanitizer also passed all 129 cases / 533,070
assertions in one process, including teardown between cases and at process exit.
Their logs contain no sanitizer findings, JUCE assertions or leaked-object
warnings. All findings from this pass are resolved.

Limits: synthetic tests do not establish physical-device hot-unplug, microphone
consent or multiple-display behavior. This pass does not repeat the separate
system heap-leak investigation documented in app-lifetime-audit.md.
