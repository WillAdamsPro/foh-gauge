# Post-merge crash and leak audit

Base: master `eca1440`, including SPL/Leq, stored-trace manager and virtual sum,
log-sweep/impulse/ETC, multi-position measurements and W7 monitoring. No open-PR
features were imported. Apple Silicon, macOS 26.2, AppleClang 21 Command Line
Tools, deployment target 26.0. Xcode is not installed; local app builds use Unix
Makefiles. No audio-callback code changed.

Findings and fixes:

- A new synthetic regression reproduced an ASan heap-buffer-overflow in core
  `virtualSum`: it indexed magnitude, phase and coherence arrays without checking
  their lengths. Reject incomplete parallel arrays and mismatched frequency
  grids. Tests exercise each missing array on either input; existing self-sum
  and cancellation tests retain their numerical assertions.
- The full Debug app suite and isolated session-replacement test reported two
  leaked `AlertWindowImpl`, `AsyncUpdater` and `StringArray` objects. Unmanaged
  asynchronous session notices could outlive their UI or remain queued when
  modal cancellation ran. Own them with `ScopedMessageBox`, close a previous
  notice on session replacement, and close on MainComponent teardown. Cover
  destruction both before asynchronous display and while a notice is modal.
- The sweep bridge test reproduced a timeout while the Debug sanitizer build
  deconvolved its million-point FFT. Give only that completion wait 15 seconds;
  retain the recovered 4.80 ms delay, impulse/ETC and silent-tail assertions.

Additional synthetic lifetime coverage creates, renders and destroys the SPL,
trace manager, impulse/ETC, TF, phase and monitoring panels 20 times, with queued
control notifications and trace deletion. Twelve engine lifetimes at 44.1/48/96
kHz process meter input and join pending sweep, calibration and delay work after
quiescing I/O. Existing suites also exercise successful sweep recovery, rolling
Leq windows, trace edits, multi-position data and active monitoring teardown.

Native leak limitation: the unsanitized Debug app's normal launch/quit under
`leaks --atExit` still reports the previously documented 10 allocations / 560
bytes rooted in AppKit `_NSWMWindowTilingStateController` and
`_NSKeyValueObservation` during JUCE window/shadow creation. No JUCE leaked-object
warnings appeared. This is not a clean native app leak result; changing private
AppKit ownership or JUCE window teardown is too risky for this focused pass.
The tool also reports restricted-process and DWARF inspection limitations.
The targeted headless session/panel/engine tests under `leaks --atExit` report
zero leaked bytes.

These synthetic tests and brief app launches do not establish real-device
hot-unplug, microphone-consent or multiple-display behavior.

Verification:

- Normal Release core gate: 140/140 CTest tests passed.
- Core ASan/UBSan, RelWithDebInfo: 140/140 passed.
- App ASan/UBSan, Debug: both FOHGauge and fohapp_tests built; 207/207
  CTest tests passed, including core, layout and golden signals.
- Direct Debug ASan/UBSan app suite: 67/67 cases, 408,400 assertions passed;
  no sanitizer or JUCE leaked-object reports.
- Targeted unsanitized Debug session/panel/engine tests under `leaks --atExit`:
  5/5 cases, 414 assertions passed, zero leaked bytes.
- Sanitized Debug app launched with an RTA floating window, then received a
  normal quit event addressed to its exact PID: exit 0, no sanitizer or JUCE
  leaked-object reports. Unsanitized Debug app also built and quit normally;
  its native leak result is recorded above.
- Public string/calibration guards and `git diff --check` passed.

ASan/UBSan use the existing `FOH_SANITIZE=address,undefined` configuration,
including JUCE modules, with nonrecovering sanitizer flags. No suppressions or
skipped tests were added. The optional W7 CPU benchmark remains opt-in; its
performance acceptance was not measured in this crash/leak audit.
