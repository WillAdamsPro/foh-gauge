# Audio-thread hardening audit

Scope: AudioEngine (declared in Widgets.h), AnalysisEngine, MainComponent,
AppModel, and the core generators, routing detector and SPSC bridges called by I/O.

| Finding | Fix and regression coverage |
| --- | --- |
| Peak updates used an unbounded weak-CAS retry loop. | At most two strong CAS attempts: the single other writer only exchanges zero. Existing peak tests plus concurrent exchange stress. |
| Device stop did not invalidate analysis or stop generator/chime/test playback. | Quiesced JUCE lifecycle callbacks cancel playback, clear peaks and advance the capture epoch. Synthetic stop/restart tests cover 44.1/48/96 kHz and 64/256/1024-frame devices. |
| Device errors were silently ignored; JUCE may report these from any thread. | Atomic error flag silences subsequent callbacks and invalidates UI operations; only lifecycle callbacks touch playback state and restart the capture handshake. Synthetic error from another thread, followed by restart. |
| Only mic routing changes reset history; reference changes and disappearing inputs could mix routing or analyze fabricated silence. | Latch both channels once per block; reset on channel/count/validity changes and stop capture for unavailable/null inputs. Publish the captured mic channel rather than a later UI selection. Synthetic reroute/missing-channel tests. |
| Independent ring push failures could offset mic and reference streams. | Producer checks space in both rings before writing either; overflow discards the block and restarts stereo history. Synthetic asymmetric-full-ring test. |
| Interrupted captures could store stale/partial traces and UI snapshots could retain results across device changes. | Message-thread epoch/error handling cancels captures (completion receives false), clears pending delay work/results and rejects old snapshots. Generator selectors get a Settings notification. Engine reset regressions cover the supporting handshake; UI completion behaviour was reviewed, not exercised against physical hardware. |
| Status could still say Measuring with an unavailable route/device; toolbar status is hidden at narrow widths. | Atomic status codes and a message-thread notice remain visible above the workspace/Setup content, with actionable setup text. Layout/code review; physical unplug and narrow-window visual checks remain manual. |
| A two-second analysis shutdown timeout could forcibly kill a worker during an expensive delay calculation. | Remove the audio callback first, then join the analysis worker without forced termination. Worker lifecycle regression tests exercise start/stop/restart. |
| Existing duplicated SPL test stubs prevented the app test target from compiling. | Remove duplicate definitions; retain the original isolated model stubs. |

The I/O callback uses no locks, I/O, logging, owning strings or std::function
copies. Loops are bounded by callback channel/frame counts; routing detection is
also capped at its fixed input limit. Callback allocation tests cover generator,
chime/detection, invalid routing, stop and variable blocks (1–8192 frames).
Calibration locks, vector/string copies, FFT/delay allocations and publication
remain on the analysis/message threads. Lifecycle mutations of non-atomic
playback state rely on JUCE's documented quiescence/serialization contract.

Validation on this Apple Silicon Mac:

- Release core build: 96/96 CTest tests passed, including synthetic golden signals.
- Release app + core build: 122/122 CTest tests passed. App binary is arm64 and
  Info.plist has LSMinimumSystemVersion 26.0.
- Core ThreadSanitizer build uses `-fsanitize=thread` in CMAKE_CXX_FLAGS and
  CMAKE_EXE_LINKER_FLAGS (the existing FOH_SANITIZE option remains unchanged).
  96/96 tests passed with no sanitizer findings (244.43 s).
- Full Xcode is absent. The requested Xcode-generator configure failed; the app
  was built locally with AppleClang 21 / Command Line Tools using Unix Makefiles
  (`build-app-local`, Release, deployment target 26.0).
- Device lifecycle tests use synthetic devices/buffers only; no physical unplug
  test or app ThreadSanitizer run was performed.
