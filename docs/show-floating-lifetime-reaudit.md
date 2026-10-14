# Show mode and floating-window lifetime re-audit

Scope: Show mode enter/exit, floating close/dock callbacks, borrowed workspace
state, and missing-display restore. No features or audio-thread changes.

The docking API accepted an ID by reference. Passing an ID from the floating
state vector let its erase replace the ID with the next panel's ID, or destroy
the string when removing the last entry. The regression reproduced incorrect
docking followed by an ASan container-overflow read in `addAsTab`. Docking now
owns the ID before modifying that vector; existing window teardown and queued
callback guards remain in place.

New synthetic Catch2 regressions cover:

- Docking IDs borrowed from an entry with a surviving sibling and the final entry.
- Rapid close/dock/recreate of the same panel with old callbacks pending,
  preserving panel identity and returning native peer counts to baseline.
- Show mode entered with RTA, SPL and Show Monitoring floating and both close
  and dock actions queued. Both Ctrl+Tab directions leave the workspace intact;
  Esc restores all panels, including Show Monitoring's original component.
- A disconnected saved display while Show mode suspends windows, restored
  bounds/pinning on exit, repeated display notifications, and persistence after
  workspace-manager relaunch.

Local platform: Apple Silicon, macOS 26.2, deployment target 26.0. This Mac has
AppleClang Command Line Tools but no full Xcode installation; builds use the
existing Unix Makefiles configurations. Release Mach-O minimum OS is 26.0.

Verification:

- Core-only Release: 165/165 CTest tests passed.
- App Release: app and tests built; 266/266 CTest tests passed.
- App unsanitized Debug: app and tests built; 266/266 CTest tests passed.
- App Debug ASan/UBSan: app and tests built; 266/266 CTest tests passed.
- Single-process Debug ASan/UBSan app suite: 101/101 cases, 530816 assertions
  passed; no sanitizer findings or JUCE leaked-object reports.
- Focused Debug ASan/UBSan regressions: 3/3 cases, 188 assertions passed.
- Public string scan and `git diff --check` passed.

The focused unsanitized Debug run under `leaks --atExit` passed all three cases
(188 assertions), with zero leaked bytes and no JUCE leaked-object warnings.
`leaks` reported restricted-process memory-inspection limitations. This is a
synthetic shell lifetime check, not a leak claim for the complete hardware app.

The full Debug app test executable prints an existing `juce_String.cpp:327`
assertion. It reproduces in the isolated passing console-window test: existing
console labels pass non-ASCII literals to JUCE's ASCII string constructor.
Console UI changes are outside this focused audit and were left unchanged.

Physical external-display unplug/reconnect and native focus during removal
remain manual checks. Synthetic display lists and real JUCE peer creation on
this Mac cover restore logic and ownership but cannot establish those hardware
behaviors. No speculative native-window teardown changes were made.
