# Accessibility and keyboard

Measurement commands appear in the Measure menu and use the same actions as the panel controls.

| Shortcut | Action |
| --- | --- |
| Space | Pause or resume measuring |
| Command-F | Find delay, using the Delay finder's Sub setting |
| Command-L | Lock the last valid delay |
| Option-Command-S | Store trace |
| Command-S | Save session |
| Shift-Command-S | Save session as |

The existing File shortcuts for Save session and Save session as are preserved. Find delay is unavailable while a search is pending; Lock delay requires a valid current-device result.

Tab and Shift-Tab traverse the toolbar, the focused panel's controls, then its plot. Workspace and panel tabs support Left/Right arrows. Panel dividers support arrow keys in five percent steps. Focus uses a two-pixel control accent outline. Hidden controls and inactive panel contents are omitted from traversal.

Painted plots, impulse arrivals, signal meters, coherence and status pills expose text values. Meter values include units and signal quality; silence is read as "No signal". Status pills expose their Setup action. Delay readouts use JUCE's accessible label values.

## Verification

Run the normal core/app suites with `ctest --test-dir build --output-on-failure`. The `[accessibility]` app tests cover live values, command mappings, floating-window dispatch, tab activation and focus retention, divider keys, and control/plot order in Dark and Sunlight at 100% and 200%.

Optional local render output (synthetic/empty states only):

```sh
FOH_ACCESSIBILITY_SNAPSHOTS=/tmp/foh-accessibility-renders build/app/fohapp_tests '[accessibility]'
```

The renders cover the toolbar, transfer function, signal meters, coherence and delay finder. At 200%, the main canvas can exceed the window and remains scrollable.

Manual VoiceOver listening and native focus-ring review by Juno/Kai remain pending. Check that pills announce their value and Setup action, readouts announce units and confidence, Tab/Shift-Tab reach visible controls, and the Measure menu displays all four shortcuts. Repeat in Dark/Sunlight at 100%/200%, including floating panels and overflowing tabs.
