# App lifetime audit

Scope: floating panels, SetupPage, RoutingWizard, workspace/session replacement,
device callback teardown and message-thread timers. No audio-thread changes.

Fixed:

- Save Workspace As grew the workspace vector while WorkspaceView retained a pointer
  to its old storage. Detach the view before growing it. The new repeated-save test
  reproduces an ASan heap-use-after-free with the detach removed.
- Embedded Inter typefaces were held in static references past JUCE shutdown. The
  expanded UI harness reported leaked CoreTextTypeface/Typeface/MemoryBlock objects
  and aborted at process exit. Give each look-and-feel instance ownership instead;
  a regression checks that destroying the owner releases all three references.

The app test target now includes MainComponent, SessionFiles, WorkspaceView,
FloatingPanels and Panels with the embedded resources. Its isolated AppModel has
no audio device, analysis worker, model timer or preference writes. Synthetic tests
exercise queued float actions after workspace changes/destruction, session open
replacing floating state, Setup device-change listener removal, and wizard timer
cancellation. Existing audio-bridge tests cover missing channels, null buffers,
device stop/error and analysis-worker teardown. Both app targets and their JUCE
modules now use the existing opt-in ASan/UBSan flags.

Local verification: Apple Silicon, macOS 26.2, AppleClang Command Line Tools,
deployment target 26.0. Xcode is not installed, so local builds use Unix Makefiles
rather than the documented Xcode generator. Debug keeps JUCE leak detection on.

- Core Release: 116/116 CTest tests passed.
- App Release and unsanitized Debug: 149/149 CTest tests passed in each build.
- App Debug ASan/UBSan: 149/149 CTest tests passed, including golden signals.
- App ASan/UBSan executable: 33/33 cases, 397208 assertions passed; no sanitizer or
  JUCE leak-detector reports.
- Sanitized Debug app: launched for five seconds and sent a normal quit request
  to its exact PID; exit 0, no sanitizer or JUCE leak-detector reports.
- Unsanitized Debug app: `leaks --atExit -- <app executable>` during the same
  launch/quit cycle reported 10 allocations / 560 bytes (exit 1). All reported
  roots are AppKit `_NSWMWindowTilingStateController`, retaining
  `_NSKeyValueObservation`, allocated through NSWindow creation, including JUCE
  shadow-window creation during default look-and-feel reset at shutdown. No JUCE
  leaked-object warnings. The tool also reported restricted-process and DWARF
  inspection limitations. This is not a clean leak result; changing AppKit/JUCE
  window teardown is deferred because it is outside a small ownership fix.

Limits: a brief app run and synthetic callbacks do not establish real-device
hot-unplug, multiple-display removal, or microphone-consent race coverage. Those
need hardware/manual verification. The isolated model does not test the real
AppModel timer's capture/delay completion ordering; the actual app smoke uses it.
