# Post-guides sanitizer audit

Base: master `42c9b91`, after PRs #97–#100. Local environment: Apple Silicon,
macOS 26.2, AppleClang 21 Command Line Tools, deployment target 26.0. Xcode
is not installed; the app builds use Unix Makefiles. Golden-signal generation
uses a Python environment with NumPy via `Python3_EXECUTABLE`.

Changes:

- The concurrent synthetic producer previously supplied 1,024 samples every
  millisecond regardless of worker progress. Debug ASan/UBSan analysis could
  overflow repeatedly and never publish an averaged frame within the test's
  three-second deadline. All three concurrent tests reproduced the failure.
  Bound queued samples in the test producer; keep the audio callback unchanged.
  The separate stereo-overflow regression still exercises overflow recovery.
- TSan reproduced a native teardown crash after passing assertions in queued
  floating actions, session replacement and the 100-window open/close stress
  case. The stack ended in `objc_msgSend`, AppKit's
  `_NSWindowTransformAnimation`, and a background display-link callback.
  Disable automatic AppKit animations on floating-panel windows before showing
  them, using the public `NSWindow.animationBehavior` API. These custom windows
  can be destroyed immediately when docking or replacing a workspace. Only
  `FloatingPanels.cpp` additionally compiles as Objective-C++ on macOS; other
  platforms retain the existing implementation. No audio-thread changes.
- Add synthetic coverage for pending Store and Verify completions after guide
  destruction, across W1–W5. A replacement guide must retain its original state
  when the old capture completes, and generator output must remain off.

The existing window lifetime/stress regressions reproduce the native failure.
Keeping JUCE initialized across the whole test process and draining messages at
exit did not resolve every case, so that partial harness change was removed.
No sanitizer suppressions, skipped cases or relaxed assertions were added.

Verification uses the AGENTS.md core and app ASan/UBSan and TSan configurations,
plus the normal Release core gate. App builds include both FOHGauge and
fohapp_tests, including their JUCE modules. TSan runs use
`TSAN_OPTIONS=halt_on_error=1`; ASan/UBSan retain the existing nonrecovering flags.
App CTest runs use four parallel processes; concurrent tests still use their
own worker/producer threads.

Results:

- Normal Release core gate: 122/122 CTest tests passed.
- Core ASan/UBSan (RelWithDebInfo): 122/122 passed.
- Core TSan (RelWithDebInfo): 122/122 passed.
- App ASan/UBSan (Debug), including core/layout/golden tests: 172/172 passed.
- App TSan (RelWithDebInfo), including core/layout/golden tests: 172/172 passed.
- Direct app suite under both sanitizers: 50/50 cases, 404,669 assertions each.
- TSan window regressions: queued actions, session replacement and 100-window
  stress each passed three consecutive runs after the fix.
- Both instrumented apps launched with `FOH_WORKSPACE=3 FOH_FLOAT=RTA` for five
  seconds and received normal quit requests to their exact PIDs; both exited 0.
- Public string scan and `git diff --check` passed.

No sanitizer or JUCE leaked-object reports appeared in the final successful
executions. No known findings from this pass are deferred.

Limits: synthetic tests and short launch/normal-quit checks do not establish
real-device hot-unplug, microphone consent or multiple-display behavior.
JUCE's Debug leak detector is checked by the app test process; this pass does
not repeat the system `leaks` investigation documented in app-lifetime-audit.md.
