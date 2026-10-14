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

## Newer-feature accelerated soak

The newer-feature suite adds deterministic bucket and worker-step accessors;
these are private friends, not runtime controls. No audio callback code changes.

- Twelve hours of actual Leq bucket bookkeeping at 8 kHz and 384 kHz, including
  three loud hours followed by nine quiet hours. The high-rate clock crosses
  32-bit sample counts. Checks cover raw accumulated energy, A/C/Z readings,
  rolling-window expiration, partial buckets and reset. Weighting filters are
  covered by the existing sample-driven tone tests; only repeated filtering is
  skipped in the accelerated bucket soak.
- Session energy now sums completed 100 ms buckets with compensated addition.
  Per-sample addition could round quiet input away after a loud session.
- A four-hour schedule of representative worker blocks and W7 comparisons uses
  the production block/command/publication paths without starting a worker or
  waiting for one-second timers. It replaces all three mic-position slots,
  saturates and drains the 64-entry queue, replaces full-size stored TFs in the
  eight-entry store, and verifies the three snapshot slots and history storage
  stay bounded. This schedule skips intervening audio blocks; the existing
  three-hour ring test and twelve-hour Leq test advance their full clocks.
- Command servicing yields after at most 64 commands per pass. A continuously
  replenished queue previously had no upper bound on work before audio analysis.
  A concurrent producer regression checks the per-pass bound and final drain;
  temporarily restoring the unbounded drain makes that regression fail.
- W7 alert state is checked for twelve hours of synthetic updates. Two hundred
  panel lifetimes and one thousand monitoring restarts verify silent-mode
  release and expired timer callbacks, with a final visible timer/plot check.
- Early/late representative block/update timings have a generous regression
  bound to detect duration-dependent work, not an absolute device CPU budget.
  Release measured approximately 99/103 microseconds per update on this Mac.

A sample-driven before/after probe seeded three hours of loud session energy,
then processed ten seconds of quiet input at 384 kHz: the previous code added
zero energy; the compensated implementation retained the expected 0.0384.

Release core/layout/golden validation: 144 tests passed (13 seconds). Release
app/core/layout/golden validation: 226 tests passed (54 seconds). Debug
ASan/UBSan validation initially passed 225/226; the sweep test exceeded its
three-second completion wait during instrumented deconvolution. Its completion
wait now allows 20 seconds (other control waits stay at three); the rerun passed
in 9.5 seconds. The strengthened concurrent queue regression also passed in
Debug ASan/UBSan. No ASan/UBSan findings were reported.

Separate follow-up: the existing session-replacement UI test passes but emits
JUCE leak-detector warnings for two asynchronous modal AlertWindowImpl objects
at teardown. That session/modal lifecycle is outside this focused soak change;
it has not been patched here.

The macOS app builds with deployment target 26.0 using Command Line Tools;
full Xcode is not installed, so the Xcode generator is unavailable. This remains
accelerated synthetic coverage, not a multi-hour hardware or live-device soak.
