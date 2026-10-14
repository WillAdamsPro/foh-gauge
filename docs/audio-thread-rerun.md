# Audio-thread safety rerun

Base: master `36432b2`, including SPL/Leq, stored traces, multi-mic analysis,
log-sweep impulse/ETC and show monitoring. Audited on Apple Silicon, macOS 26.2,
AppleClang / Command Line Tools, with deployment target 26.0.

No production data race was identified in this pass. The existing ownership and
handoffs remain appropriate; this change strengthens regression coverage rather
than changing working DSP or adding synchronization to the audio callback.

| Path | Ownership / synchronization reviewed |
| --- | --- |
| Audio capture and restart | Audio is the sole ring producer; analysis is the sole consumer. Lifecycle/routing/overflow changes advance `captureEpoch`. Analysis drains old samples and rebuilds history before releasing `analysisEpoch`. JUCE serializes lifecycle callbacks with I/O. |
| Generator and sweep | Non-atomic playback state belongs to I/O and quiesced lifecycle callbacks. Message/analysis requests, automatic sweep completion and silent monitoring use lock-free atomics. Sweep capture, deconvolution and result vectors belong to analysis. |
| TF/RTA, delay, multi-mic and Leq | DSP state belongs to analysis. Settings and capture commands use the single-producer CommandQueue. Leq invalidation uses an atomic revision. Mic calibration uses a mutex only between the message and analysis threads. |
| Snapshots and meters | Triple-buffer exchange publishes fully owned results to one message-thread reader. The reader copies any data retained across `latest()` calls. Peaks use bounded atomic updates and message-thread exchanges. |
| SPL calibration, targets and stored traces | Calibration maps, target parsing/plotting, trace mutations and virtual sums belong to the message thread. SPL changes invalidate worker meters through the atomic Leq revision; their maps never enter I/O. Targets never enter analysis. Stored traces own copies of snapshot vectors. |
| Shutdown | Remove/quiesce the audio callback, then join analysis before destroying buffers or worker state. |

The callback audit found only bounded sample/channel loops, generator arithmetic,
preallocated routing/chime state, lock-free atomics and SPSC writes. No callback
locks, allocations or file/device I/O were introduced. Allocation counting also
runs inside every callback of the concurrent synthetic producer, including actual
generator output buffers (the earlier harness supplied no outputs).

Focused synthetic concurrency coverage:

- Existing capture reset/pause/resume and settings/mic-calibration tests continue
  running alongside analysis and snapshot reads. Calibration coverage now also
  changes the atomic Leq revision and checks the worker acknowledgement and levels.
- Device stop/restart while a fresh capture and log sweep are active must clear
  old averages, meter validity and impulse data, then resume fresh measurements.
- Sample-rate changes mid-sweep must cancel the sweep and publish measurements at
  the new rate/epoch without retaining the old impulse.
- Pink/sweep/off changes during cumulative TF averaging must produce finite,
  allocation-free output, including audible playback and silence after stopping.
- Live meter/TF snapshots overlap target selection, offset/tolerance changes,
  stored-trace edits and virtual sums. Plot content and copied trace data are
  checked while the producer and worker continue running.

The UI test uses the existing isolated AppModel constructor and manually copies
real analysis snapshots on the sole reader, matching the model timer's ownership.
Target controls and stored-trace methods run their production implementations.
The test does not exercise the stubbed SPL calibration methods or model timer;
those production paths were reviewed. No hardware, user calibration files or
user preferences are used by these tests.

The parallel Release run exposed a separate test-fixture filesystem race:
`getNonexistentChildFile()` did not reserve the shared workspace-test directory.
Two concurrently starting tests could choose the same name and overwrite or
delete each other's layouts (the missing-display regression restored one floating
panel instead of three). Give each workspace-restore fixture a UUID directory.
The existing restore regressions cover the failure; no production workspace
behavior changed. Both affected restore cases passed 20 repetitions each in
parallel after the fix.

An earlier TSan sweep run exceeded the generic three-second completion wait
while full-length deconvolution was running. Allow 30 seconds for that one
expensive worker result; other handoff deadlines remain three seconds. The
recovered 4.80 ms delay, impulse/ETC, automatic stop and output-tail assertions
are unchanged. The dedicated TSan sweep rerun passed all nine assertions, followed by three
consecutive successful CTest repetitions.

Validation:

- Release core: 139/139 CTest tests passed.
- Release app and tests: 212/212 CTest tests passed; arm64 app,
  `LSMinimumSystemVersion` 26.0.
- App + core TSan (`FOH_TSAN=ON`, RelWithDebInfo): 212/212 CTest tests
  passed, with no sanitizer reports (128.04 s).
- Focused `[concurrent]` TSan suite: 7/7 cases, 716 assertions passed; no
  race reports or callback allocations.

Builds use Unix Makefiles because this Mac has Command Line Tools rather than
full Xcode. Both builds compile the app and JUCE modules; TSan runs set
`TSAN_OPTIONS=halt_on_error=1`, without suppressions or skipped tests.

No known data races are deferred from this audit. Synthetic coverage does not
establish physical device hot-unplug behavior or exhaust every thread schedule.
Lifecycle callbacks must continue to obey JUCE's serialized I/O contract, and the
command/snapshot bridges must retain their single-producer/single-reader use.
