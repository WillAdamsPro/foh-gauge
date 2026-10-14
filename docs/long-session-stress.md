# Long-session and UI stress verification

Audited on Apple Silicon, macOS 26.2, JUCE 8.0.4.

Session storage is bounded already: stereo audio queues have 524,288 samples
per channel and drop whole overflowing blocks; analysis delay histories recycle
three seconds per channel; TF reference history, FFT input buffers and averaging
arrays are fixed size. The snapshot publisher recycles three slots and the
command queue has 64 slots. Captures use the eight-entry TraceStore; session
loading also rejects more than eight captures. Plot series, annotations and
guide summary rows are replaced or cleared before each update. SPLHistory,
Dose and Events are currently placeholders, with no accumulated history or log.
There is no persistent runtime logger. User-created workspaces and calibration
records are explicit saved configuration, not an automatically growing history.
No audio callback changes were needed.

Fixes and regression coverage:

- Live workspace references survive repeated Save As operations, including
  references held by floating windows. The live collection now uses a deque.
- Floating-window opening is idempotent and suppressed during Show mode.
- Restored layouts reject duplicate panel owners; empty display work areas
  preserve the last position until a usable display is available.
- Tab activation ignores invalid paths/indices and requests during Show mode
  or after the workspace has detached.
- Bundled fonts belong to the look-and-feel instance. Static ownership formerly
  survived JUCE shutdown, triggering Typeface/MemoryBlock leak warnings and a
  mutex error on quit. Tests now link the same bundled font data as the app.

Validation:

- Release core/layout/golden tests: 109/109 passed.
- Release app plus core/layout/golden tests: 134/134 passed.
- ASan/UBSan core/layout/golden tests: 109/109 passed.
- Three hours of synthetic 48 kHz blocks advanced through the actual ring
  cursors, with full-queue drops and recovery; 10,000 trace-store recycling cycles.
- 1,000 forward/reverse preset selections with Show mode toggles, narrow/wide
  rebuilds and panel instance counts; 100 native floating-window open/close
  cycles checking peer counts and detached content ownership.
- Debug UI stress tests under macOS `leaks --atExit`: zero leaked bytes and no
  JUCE leak-detector warnings. A full Debug app launch/normal JUCE quit probe
  also reported zero leaked bytes, with no assertions or shutdown exception.
  The probe used a temporary one-second quit timer removed before committing.

Builds used the installed Command Line Tools with deployment target 26.0; full
Xcode is not installed here, so the Xcode generator could not be used.
Multiple/missing displays are covered synthetically, including negative display
origins and smaller fallback screens. A physical display-unplug test and reliable
held-key input delivery remain manual checks; the automated UI harness invokes
the preset command manager and workspace view directly. Native Show toggling
was also exercised in the full Debug app. This is accelerated stress coverage,
not a wall-clock multi-hour audio-device soak.
