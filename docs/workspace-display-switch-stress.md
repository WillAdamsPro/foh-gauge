# Workspace/display switching hardening

Floating windows forward the existing cycling and UI-scale shortcuts
to the shell and unregister keyboard listeners before teardown; an empty dock
root now restores after the last panel is floated/closed. Empty nested leaves
and missing/non-string panel IDs remain invalid. Plots skip painting when
workspace collapse or scaling leaves no room after axis/legend padding, avoiding
negative drawing dimensions during these transitions.

The existing session lifetime fixture now drains queued notices before cancelling
modal components, so its test cleanup does not strand alert objects at shutdown.

The automated shell stress test makes 48 switches through Cmd+1–4 command
dispatch, Ctrl+Tab, Ctrl+Shift+Tab and the top-bar switcher. A single synthetic
audio producer feeds the real analysis worker while its snapshots are delivered
to the UI. The existing app-test AppModel stand-in supplies capture presentation
state; averaging resets run on the real worker. Hardware I/O and the production
AppModel capture-completion timer are not exercised by this test.

The test keeps a floating panel in every preset, checks stable panel identities,
toggles pinning and 100/200% scale, enters/exits Show mode, and verifies stale
dock/close callbacks expire after window replacement or shell destruction.
Desktop peer counts return to baseline after teardown. A separate native JUCE
peer key-event regression covers scale and workspace shortcuts with floating
panel focus, including destruction of the event's window during cycling.

Display-list tests simulate repeated unplug/reconnect across all presets,
transient empty/invalid display lists, a smaller main display at a nonzero
origin, and persistence/relaunch. Existing window tests cover repeated peer
creation/destruction and parent-size-change notifications on this Mac's actual
display list.

Manual checks remaining:

- Run capture in the installed app and rapidly switch from both docked and
  floating panel focus. Check capture completion and guide callbacks with the
  production AppModel timer.
- Unplug a real external display containing several pinned/unpinned floating
  windows during switching. Verify reachable title bars, docking, restored
  positions after relaunch, and native focus after reconnection.
- Visually inspect 100–200% scale and Show mode while switching, including
  readable controls and native focus on each display.
- Expert toggling cannot be checked: task 37's global Expert toggle is absent
  from this checkout. No new toggle was added by this hardening pass.

Local validation on Apple Silicon, macOS 26.2, using AppleClang/Unix Makefiles
with deployment target 26.0:

- Core Release: 144/144 CTest tests passed.
- App Release and Debug: both apps built; 227/227 CTest tests passed in each
  build. The final app executables each passed 83/83 cases (447899 assertions).
- The final Debug executable emitted no JUCE assertion or leaked-object reports.
- Optimized ASan/UBSan (`RelWithDebInfo`): both apps built; core/layout/golden
  CTest 144/144 and app CTest 83/83 passed. The app executable also passed
  83/83 cases (447899 assertions), with no sanitizer findings.
- The public string scan and `git diff --check` passed. The Release Mach-O
  reports minimum macOS 26.0.

Unoptimized Debug ASan/UBSan hit the existing sweep test's three-second
completion timeout, including in isolation. That DSP/test timing change is
outside this shell pass; no timeout or sweep implementation was changed.

## Additional display-move regression coverage

Ordinary floating-window moves now refresh resize limits for the destination
work area, including work areas smaller than the usual panel minimum. Updating
these limits does not reposition the window during a drag. Move/resize callbacks
preserve the last display ID if JUCE temporarily has no display to resolve.
Synthetic small/large/tiny display snapshots exercise repeated limit changes;
a native move callback checks that stale limits are replaced without requiring
a topology notification. The shell stress test also alternates 1x1 and
8000x5000 content sizes and checks unsaved calibration text and capture state
through switching and Show mode transitions.

Physical external-display unplug/reconnect and native focus checks remain manual
as described above; synthetic snapshots do not emulate AppKit hotplug ordering.

Validation for this additional pass (Apple Silicon, deployment target 26.0):
core Release CTest 203/203 passed; app Release build and full CTest 407/407
passed; Debug app/tests built and the workspace/lifetime subset passed 42 cases
(24049 assertions), with no JUCE assertions or leaked-object reports. The
public string scan and `git diff --check` also passed.
