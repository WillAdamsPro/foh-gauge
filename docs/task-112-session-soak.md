# Task 112: session lifecycle soak

Base: `1af7723` from origin/master. Apple Silicon, macOS 26.2, JUCE 8.0.4,
AppleClang Command Line Tools, deployment target 26.0. Synthetic devices/data only.

## Reproduced findings

Opening a session whose custom workspace ID differed from its name-derived
slug created another local workspace on every open. Before the fix, two opens
increased the manager from four to six entries, persisted both duplicates,
and never registered the original ID. Session import now preserves custom IDs;
unknown non-custom IDs get a deterministic, short custom ID discoverable at
relaunch. Import saves the actual layout rather than a copy of Setup.

The regression opens the original bundle 128 times, checks exactly one imported
workspace and two custom layout files, then checks identity after manager
relaunch. A manager regression also covers unknown IDs at the schema length limit.

Hidden Setup navigation requested keyboard focus before window attachment.
A new synthetic regression reproduced six JUCE focus assertions. Section/scroll
selection is now preserved while focus is requested only for showing controls,
including the SPL calibration control. The same regression then passes without
assertions. CTest now rejects JUCE assertion and leaked-object diagnostics;
previously those could accompany a zero test exit code.

## Combined soak

Each UI iteration drains a macOS autorelease pool, matching event-loop cleanup.
256 iterations use the production audio callback, SPSC buffers, synchronous
analysis worker stepping, model snapshot polling, and trace storage. Each cycle:

- Measures synthetic pink noise with a 1024-point FFT and fills all eight traces.
- Builds a virtual sum, toggles pink/sweep generation, stops it, and checks silence.
- Switches among all four presets, floats RTA, enters/exits Show mode and docks it.
- Saves, clears, reloads and clears the session captures.
- Checks stable panel identities, exact listener/component counts after warm-up,
  no remaining floats or modal components, desktop peer count, bounded workspace
  file count, and absence of save recovery directories.

`storeTrace` moved unchanged into the shared measurement translation unit so
the app test links production storage instead of a throwing test stand-in.
No audio-thread code changed. The isolated model skips hardware setup, its
automatic timer, and guided-capture orchestration; snapshots are polled explicitly.
This is accelerated lifecycle coverage, not a wall-clock live-device soak.
The existing four-hour representative DSP/monitoring schedule also runs in `[soak]`.

## Harness corrections

Concurrent builds exposed session fixtures choosing the same nonexistent folder
before either created it. Those fixtures now use UUID paths.

The toolbar test needed a visible, non-animating window for focus actions and
explicit delivery of pending theme notifications. A fixed 30 ms wait could expire
before instrumented layout applied the theme. The editable-count test likewise
needed a visible window before opening its text editor. Both native window
fixtures disable close animations, matching the app's floating-window policy.

A full single-process app run exposed the W6 guide helper's fixed 10/20 ms waits
returning before confirmation creation/click completion. Message-queue barriers
now wait for the posted dialog, button command and modal callback, with bounded
waits and safe callback ownership. Product generator confirmation is unchanged.

## Reproduction

Use the AGENTS.md core Release, app Release, and app Debug ASan/UBSan build
commands. This Mac uses Unix Makefiles because full Xcode is not installed.
The existing sanitizer cache uses `-g -O1` for core Debug compilation; JUCE
app/test targets retain Debug assertions and leak detection at `-O0`.

```sh
ctest --test-dir build --output-on-failure
ctest --test-dir build-app --output-on-failure
ctest --test-dir build-app-sanitize --output-on-failure
build-app-sanitize/app/fohapp_tests '~[soak]'
build-app-sanitize/app/fohapp_tests '[soak]'
MallocStackLogging=1 leaks --atExit -- build-ui-stress-debug/app/fohapp_tests '[soak]'
```

The final command uses a separate unsanitized Debug app/tests build. Inspect
successful-test output and direct-run output for diagnostics as well as Catch2
results. The two filters partition the app suite into normal cases and soaks.

## Results

- Release core gate: 203/203 passed (35.51 s).
- Release app/core/layout/console/golden: all 410 cases verified. The full run
  passed 409/410 (143.10 s); the shared-session fixture failure was corrected
  and its rerun passed (1/1, 2.24 s).
- Debug ASan/UBSan: 203 core cases and all three app soaks passed. The combined
  lifecycle soak passed in 594.06 s; the repeated-import soak in 112.85 s.
- Debug ASan/UBSan normal app cases: all 204 verified. The strict diagnostic
  run passed 202/204 (81.79 s); after the two remaining fixture corrections,
  both reruns passed (2/2, 12.63 s). No sanitizer finding occurred.
- Standalone Release `[soak]`: 3 cases, 9,865 assertions passed. Four-hour
  representative DSP updates measured about 119 microseconds early and 123 late.
- Unsanitized Debug `[soak]` under `leaks --atExit`: 3 cases / 9,865 assertions
  passed; zero leaks / zero leaked bytes. Reported physical footprint peaked
  at 109.9M, with no JUCE assertions or leaked-object warnings.
- A separate native heap check of the corrected visible-window fixtures and
  hidden-Setup regression passed 3 cases / 259 assertions with zero leaked bytes.
- The built Release app is arm64 and reports `LSMinimumSystemVersion` 26.0.
- Final single-process Debug ASan/UBSan run excluding the separately verified
  soaks: 204/204 cases, 3,388,774 assertions passed, with no JUCE assertions,
  leaked-object warnings, ASan reports or UBSan reports. Together with the three
  soak cases and 203 core cases, validation covers all 410 registered tests.
