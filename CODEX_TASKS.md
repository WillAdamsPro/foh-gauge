# CODEX_TASKS.md: prioritized, self-contained coding tasks
Read AGENTS.md first. Every task: branch from master, one PR, merge-commit on green CI.
Default test command (T): `cmake -S . -B build -DFOH_BUILD_APP=OFF && cmake --build build -j && ctest --test-dir build --output-on-failure`
App tasks also require the macOS build (dispatch build-macos on master after merge, or build locally on a Mac).

## P0: build 0 (Thu Oct 16), integrator only
0. Merge queue #16-#18, #27-#30, #31-#37, #40, #45, #26, #38; build-macos green with LSMinimumSystemVersion 26.0; test tag, then v0.0 prerelease.

## P1: app (UI queue)
1. Replace `app/**/QaAdapters.h` with core APIs. Files: QaAdapters.h (delete), its includers in app/. Use classifyCoherence (>=0.9 green, 0.6-0.9 amber, <0.6 or NaN red), setConvergenceTimeout(10 s), subDelayFromPhase, double reference delay, DelayResult::ok(). Accept: no QaAdapters.h, app builds, T passes.
2. MainComponent fixes. Files: app/MainComponent.*. Clear the stereo history ring on sample-rate change; run findDelay async via CommandQueue (never on the audio thread); use fetch_max for micPeak; keep 3 s of sub history. Accept: macOS build green; no allocations or locks on the audio thread.
3. Ctrl+Tab / Ctrl+Shift+Tab cycles workspaces. Files: app workspace switcher. Accept: wraps around, matches Cmd+1-4 order.
4. Show mode: minimal RTA/Show view hiding the inspector and helper. Accept: toggle persists across relaunch.
5. Venue-mention scan: add `scripts/check-strings.sh` run in linux CI, grepping app/, README and resources for venue/church terms (wordlist in the script). Accept: CI fails on a planted term.

## P2: packaging and CI
6. #38 release workflow: create releases with `prerelease: true` and plain notes; attach zip, DMG and SHA256SUMS. Accept: a test tag produces a prerelease with all three assets.
7. #26 macOS 26.0 target: resolve conflict with merged #42 (keep #42's triggers/concurrency, take #26's deployment target and runner).
8. Block real cal files: .gitignore `*.cal`, `*.frd`, `*-cal.csv` except `tests/data/synthetic-*`; CI step failing on any tracked match. Accept: CI fails on a planted `foo-cal.csv`.
9. JUCE fallback: if Xcode 26 breaks JUCE 8.0.4, branch `ci/juce-bump` pinning 8.0.15 in CMakeLists.txt. Accept: build-macos green.

## P3: tests (simulated room)
10. #43 review fixes: rename rounding helper to "half away from zero", document it as message-thread only, move it to core format.h/.cpp, add edge tests (x.xx5, negatives, NaN/inf).
11. After #45 merges: drop the xfails in #43/#44 that #45 fixes.
    - Deferred on master: #45 is merged (`4341c11`), but #43 and #44 remain open drafts; their `test_part_a.cpp` and `test_room_sim.cpp` tests and `[!shouldfail]` markers are not present here. Do not import the stacked drafts just to remove their xfails.
    - Once those tests land, remove the fixed D-SGN and S16 reflection-level xfails. D-SGN tests must explicitly enable negative-lag search with `DelayOptions::minDelayMs < 0` (or symmetric search); the default still searches positive lags only. Update the Part A coverage map to reflect passing tests and run T. Master's existing S16 rate-independence and D-SGN regression tests already assert #45's fixes without xfails.
12. Rebase #39/#41/#43/#44 onto master using `tests/data/synthetic-mic-cal.csv` (#46); update expected values. Accept: T passes.

## Build #1 (Oct 25, hard window Oct 25-27)
13. Workspace presets Setup / Alignment / Tuning / RTA-Show with Cmd+1-4. Accept: each preset restores its panel set.
14. Tear-off floating panel windows with per-display restore and fallback to the main display when the saved display is missing. Accept: relaunch restores positions; unplugging a display does not lose windows.
15. Confirm settle detector, traffic light and two-spike flag are wired in UI per the QA decisions (settle <0.5 dB RMS x3 with >=25% coherent bins, 10 s timeout warning; two-spike within 3 dB and >0.5 ms apart).
Not in build #1 (superseded below where listed): export, live delay tracker, user layouts, drag-to-dock, Windows.

## Build #1 P1: v0.0.1 feedback (do these first)
16. BUG: mic permission prompt repeats. Files: app/CMakeLists.txt (stable BUNDLE_ID), .github/workflows/build-macos.yml + release.yml (ad-hoc sign: `codesign --force --deep -s - "$APP"` before zip/DMG), app/Source/AudioEngine.* or Main.cpp (check AVCaptureDevice authorizationStatus; request only when NotDetermined, never on each device open). Accept: install, grant once, relaunch and switch devices 3x: no second prompt; `codesign -dv` shows an ad-hoc signature and the bundle id. Test: build-macos green; manual on Mac.
17. BUG: UI scale. At 200% (MainComponent.cpp ~28-32, setGlobalScaleFactor) the window grows huge and 100% is unreachable; the scale box is hidden when nw==0 (~line 207). Fix: keep the window size and scale the content inside it (AffineTransform on the content component, not the global scale factor); clamp the window to the current display's user area; scale and theme always reachable (View menu; Cmd+0 = 100%, Cmd+= / Cmd+- step); if the saved scale doesn't fit at startup, use the largest scale that fits, else 100%. Files: app/Source/MainComponent.*, Main.cpp. Accept: at 200% on a 1512x982 display the window stays on-screen and Cmd+0 restores 100%. Test: build-macos green; manual.
18. First-launch Setup page: input/output device, channels (mic, ref, gen), sample rate; shown on first launch and from the toolbar. Replace the output test tone with a short pleasant chime (synthesized, e.g. 2-3 decaying sine partials, -18 dBFS peak). Files: app/Source/SetupPage.* (new), MainComponent.*, app/CMakeLists.txt. Accept: fresh prefs open Setup; Done persists and is skipped next launch; chime is generated off the audio thread and played RT-safe.
19. Routing Easy Setup, part 1 (Setup page button): step-by-step plain-language walkthrough, one connection per step with a simple diagram: measurement mic input, loopback reference input, output to console/PA, and what each does. Files: app/Source/RoutingWizard.* (new), SetupPage.*, app/CMakeLists.txt. Accept: wizard writes the channel map used by AudioEngine; Back/Next/Skip work.
20. Routing Easy Setup, part 2: auto-detect interface and suggest defaults from a preset table (core/src/interface_presets.cpp, pure data; e.g. UMC404HD: mic In1, ref In2, Out2->In2 loopback, Out1 to console); unknown interface = manual pick. Per-connection test: play chime or pink noise on the chosen output and check signal arrives on the expected input (and not only on the wrong one), pass/fail per step. Accept: core test for preset lookup by device name; detection logic tested with synthetic buffers (signal on expected channel = pass, swapped = fail with "swap inputs" hint). Test: T.
21. One workspace switcher: keep the top-bar switcher, remove the left one. Files: app/Source/MainComponent.cpp, WorkspaceView.*. Accept: one switcher; Cmd+1-4 and Ctrl+Tab still work.
22. SPL calibration (replaces "Sens Factor shown, not applied"): Setup + toolbar entry; calibrator step (94 or 114 dB at 1 kHz) or enter a known level; stores dB SPL offset per input; RTA/level readouts in dB SPL when calibrated; mic-cal Sens Factor applied relative to the calibrated offset. Files: core (SplCal in fohcore.h + src/splcal.cpp), app Setup page, Panels.cpp. Accept: core test: synthetic 1 kHz at -20 dBFS calibrated as 94 dB reads 94.0 +/- 0.1; uncalibrated shows dBFS. Test: T.
23. Session files per venue: Save / Open / Save As; JSON manifest plus folder (`*.fohsession/`: session.json, cal file copy, captures/*.json, targets). Contents: devices, channel map, mic cal, SPL cal, targets, captures, workspace. Files: core/src/session.cpp (+ header, pure data), app menus. Accept: core round-trip test (save, load, compare); missing device on open falls back to Setup with a notice. Test: T.
24. Target traces on TF magnitude and RTA: import Smaart ASCII (both formats in resources/targets/: 4-column TF export and header+2-column target with Tolerance), ship 3 built-ins from resources/targets/target-plus{6,9,12}.txt, optional tolerance band. Files: core/src/target.cpp (parser), app overlay in Panels.cpp, app/CMakeLists.txt (binary data). Accept: parser tests on all 4 files (point count, first/last values, tolerance 3.0); overlay toggles per panel. Test: T.

## Build #1: remaining
25. Show mode follow-ups: Ctrl+Tab must not cycle workspaces in Show mode (it currently runs before the showMode guard in MainComponent::keyPressed); Esc exits Show mode. Accept: manual + build-macos green.
26. Settle UI wording: "unsettled" shows only after the 10 s timeout or a capture that ended unsettled, and "averaging..." before that (check Panels.cpp TF note after #57). Accept: app test in tests/test_audio_bridge.cpp or manual.
27. W2 sub-delay alternatives UI: show primary and the +/-1-period alternatives as selectable rows, raw impulse peak labelled "hint only", polarity-flip option. Files: app/Source/GuidePanel.cpp. Accept: synthetic 12.5 ms sub case shows 12.5 primary with 2.5/22.5 alternatives at 100 Hz XO.
28. Two-spike flag UI: warning badge on the delay panel + confidence shown x0.75 when flagged (core already applies it); tooltip explains "only one speaker on?". Files: Panels.cpp. Accept: manual with the twospike golden pair.
29. Floating windows: verify per-display restore and missing-display fallback on a real 2-display Mac; fix anything found. Files: FloatingPanels.*, LayoutModel.*. Accept: tests/test_workspace_restore.cpp green + manual unplug test.
30. Room-sim harness in CI: salvage Juno's RoomSim (#44: tests/roomsim/room_sim.h, tests/test_room_sim.cpp) onto master with synthetic cal only, drop xfails fixed by #45, run in linux-tests. Accept: T passes in CI.
31. Golden signals: salvage #39's generator (tools/gen_golden.py) but generate WAVs at build time instead of committing ~70 binaries; golden_tests ctest. Accept: T passes in CI.
32. Venue/church string scan and cal-file guard also run in linux-tests (PR gate), not only build-macos. Files: .github/workflows/build-macos.yml. Accept: a PR with a planted term or `foo-cal.csv` fails linux-tests.

## Post-build #1 (numbered, priority order; checked against room-app concept v3)
Already on master (skip): guided W1 reference delay, W2 sub-to-mains (phase delay + alternatives), W3 fill alignment in GuidePanel; stored traces (storeTrace/findTrace); core virtualSum(); core generators pink/sine/log sweep; core impulseFromTF; settle/coherence/two-spike core logic.
33. Guided workflow frame + Apply-then-Verify: common step frame for W1-W5 (Apply = user enters change in DSP, Verify = re-measure and compare against the stored trace with a pass/fail note). Files: app/Source/GuidePanel.*. Accept: every W step ends in Verify; never auto-applies.
34. Easy Setup questionnaire (optional, never forced): clusters, cabinets per cluster, individual vs grouped control, front fills, subs and arrangement; result tailors which W steps appear. Files: app/Source/EasySetup.* (new), core/src/rig_profile.cpp (pure data + JSON, saved in session). Accept: core test mapping 3 sample rigs to step lists; Skip leaves the default flow.
35. Mic placement diagrams per step (simple vector drawings: on-axis, seam, sub/main crossover position). Files: app/Source/Diagrams.*. Accept: each W step that needs a mic move shows a diagram.
36. W4 tuning + W5 pre-doors check in the guide (SPL cal -> time alignment -> tuning order; W5 5-minute check with symptom-to-cause list). Files: GuidePanel.cpp. Accept: manual walk-through on loopback.
37. Expert toggle: one global toggle revealing all charts and settings; guides still work in Expert. Files: MainComponent.*, AppModel.*. Accept: persists; default off.
38. Interface loopback correction: measure the interface's own loopback (Out->In) once, store it, and subtract it from TF (latency + response). Files: core/src/loopback.cpp, app Setup. Accept: core test: synthetic interface response removed to +/-0.1 dB.
39. SPL/Leq meter (indicative): A/C/Z, Leq 1 min/15 min/session, WHO and NIOSH presets with warnings. Needs task 22 (SPL cal). Files: core/src/leq.cpp, app panel. Accept: core tests: 94 dB 1 kHz reads 94.0 (A and Z); Leq of constant signal = level.
40. Stored-trace manager + virtual sum UI: name/colour/show/hide/delete, and a virtual sum preview of two stored traces (core virtualSum exists). Files: Panels.cpp, AppModel.*. Accept: sum of a trace with itself reads +6.02 dB.
41. Multi-mic: 2-3 positions, per-position time alignment, coherence-weighted multi-position average against the target. Files: core/src/multipos.cpp, AnalysisEngine, app. Accept: core test: weighted average favours the high-coherence position; per-position delays applied.
42. Log sweep + impulse view: sweep playback with safety ramp, deconvolution against the loopback reference, impulse/ETC plot. Files: core (sweep deconvolution), app panel. Accept: core test recovers a synthetic 4.80 ms delay +/-0.02 ms.
43. W6 per-box polarity check (impulse direction near-field). Files: core (polarity from impulse), GuidePanel. Accept: core test on inverted/normal synthetic impulses.
44. W7 show-time monitoring: low-overhead live TF/RTA against stored reference during the show with drift alerts (no generator). Files: app panel. Accept: manual; CPU < 10% on M1.
45. Console control: Allen & Heath SQ (SQ-5, firmware 1.6.3) and Behringer X32 (OSC): read/write EQ and delay on mix buses and matrices, with confirm-before-write. Files: core-independent module app/Source/console/*. Accept: protocol unit tests against recorded messages; no writes without confirmation.
46. Windows port: CMake/JUCE Windows build, ASIO/WASAPI device handling, Windows CI job. Accept: CI artifact launches; core tests pass on Windows.
Superseded: the earlier PHASED PLAN items are now tasks 34-35 (Easy Setup), 41 (multi-mic) and 45 (console control).

## Review fixes (batch #66-#80)
47. Menu bar after the #67/#71 merge: the macOS menu bar is owned by the Window (File + View); File delegates to MainComponent. Restore a non-mac menu bar (setMenuBar) for the Windows port and add Cmd+S / Cmd+Shift+S / Cmd+O as menu command keypresses instead of raw keyPressed handling. Files: app/Source/Main.cpp, MainComponent.*. Accept: File and View menus both work on macOS; shortcuts appear in menus; build-macos green.
48. Session restore of channel routing: InputsAndDevicesPanel::loadState now writes engine channels and notifies AppModel::Settings (the old sync() was removed in the merge). Add an app test that loading a session updates SetupPage selections and AudioEngine channels. Files: app/Source/Panels.cpp, tests/test_session.cpp or tests/test_audio_bridge.cpp. Accept: test passes in build-macos.
49. Mic consent denied: after #66 the window opens silently when access is denied. Show a one-line banner with a button that opens System Settings > Privacy > Microphone. Files: app/Source/Main.cpp, MainComponent.*. Accept: manual with consent reset (`tccutil reset Microphone com.willadams.fohgauge`).
50. Chime routing: the Setup chime plays on both generator outputs, including the loopback output. Play it only on the output under test, so the routing wizard (tasks 19/20) can tell outputs apart. Files: app/Source/AudioEngine.*, SetupPage.cpp. Accept: tests/test_audio_bridge.cpp chime case checks a single selected output.
51. Mac smoke test of the merged UI (scale 100-200%, Show mode + Esc, single switcher, Setup page, targets overlay, session save/open, routing walkthrough) on Will's Mac with Atlas's approval; file issues as tasks. Accept: checklist in docs/ (no venue data).
