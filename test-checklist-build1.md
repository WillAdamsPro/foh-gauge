# FOH Gauge — Test Checklist, Build #1 (target ~Oct 22–25, 2026)
Owner: QA - Echo · Builder: Vega · Host: Will's Mac + Behringer UMC404HD

> **GATING:** Any test on Will's Mac (install, launch, device access, audio I/O) needs **Will's explicit permission each time**. Don't carry permission over from an earlier session. Log who approved it and when in the results table. Synthetic and file-based tests can run on the box first.

## 0. Setup and references
- Stimuli: `/workspace/audio-qa/venv/bin/python /workspace/audio-qa/gen_stimuli.py <out> <sr>` at 44100, 48000 and 96000. It produces pink noise (-12 dBFS RMS), MLS (order 16), a log sweep with `logsweep_inverse`, white noise, an impulse, and known-filter pairs: `known_reference` vs `known_peq1k_+6dB_Q1`, `known_hpf100_2nd` (Butterworth), `known_delay_4.80ms_frac` and `known_delay_230samp`.
  - Note: 4.80 ms at 48 kHz is **230.4 samples**, so the exact file uses a fractional (FFT) delay. The integer file is 230 samples, or 4.7917 ms.
- Reference: `reference_tf.py ref.wav meas.wav --csv out.csv` gives the delay (sub-sample), magnitude in dB, phase in degrees (delay-compensated) and coherence (H1, Welch). Cross-check with REW (free) and pyfar.
- Record the app version/build hash, macOS version, CPU type (Intel or Apple Silicon), buffer size, sample rate and FFT/averaging settings for every result.

## 1. Synthetic signal tests (loopback or file input)
Tolerances: **TF magnitude ±0.5 dB**, **phase ±5° wherever coherence > 0.9**, **delay finder ±0.05 ms or ±1 sample** (whichever is larger), **RTA band level ±0.5 dB**.
| ID | Test | Expected | Tol |
|---|---|---|---|
| S1 | Ref vs identical copy | 0 dB, 0°, coh ≈ 1, delay 0 | as above |
| S2 | +6 dB peaking EQ at 1 kHz, Q 1 | +6.00 dB at 1 kHz; about +1.9 dB at 500 Hz and 2 kHz; 0° at 1 kHz | ±0.5 dB / ±5° |
| S3 | 2nd-order HPF at 100 Hz | -3.0 dB at 100 Hz; about -8.6 dB at 63 Hz; about -20.3 dB at 31.5 Hz; +90° at 100 Hz | ±0.5 dB / ±5° |
| S4 | Delay of 4.80 ms (fractional, 48 kHz) | 4.80 ms / 230.4 samples | ±0.05 ms |
| S5 | Delay of 230 samples (integer) | 4.7917 ms | ±1 sample |
| S6 | Polarity-inverted reference | 180° / polarity flag | ±5° |
| S7 | Gain of -6 dB and +12 dB | flat -6 / +12 dB | ±0.5 dB |
| S8 | Uncorrelated noise added (SNR 0/10/20 dB) | coherence drops; app masks or flags data where coh < threshold | qualitative |
| S9 | RTA: pink noise at -12 dBFS RMS | flat in 1/3- and 1/1-octave bands (pink); bands match the scipy band-filter reference | ±0.5 dB |
| S10 | RTA: 1 kHz sine at 0 dBFS | shows in the 1 kHz band at the correct dBFS level; no leakage over 1 band | ±0.5 dB |
| S11 | Stimulus types: log sweep + inverse, MLS, pink noise | the same DUT gives the same TF with each | ±0.5 dB |
| S12 | Repeat S1–S5 at 44.1 and 96 kHz | as above (scale the samples) | as above |
| S13 | Long delays (50, 200, 500 ms) and the edge of the search window | found, or a clear "out of range" message | ±0.05 ms |

| S14 | Polarity + delay ambiguity: lowpassed (100 Hz, LR4) "sub" vs highpassed "main", offset by 5 ms with sub inverted, vs 10 ms non-inverted | app (or GW) does not lock onto the wrong cycle; it presents ±½-cycle/INV alternatives or picks max-summation | sum within 3 dB → ±1.25 ms |
| S15 | Band-limited sub signal (30–120 Hz) with 2 ms DSP-style group delay | delay finder value is labeled as an impulse-peak estimate; phase-slope readout agrees with reference_tf in the crossover band | ±5° where coh>0.9 |
| S16 | Synthetic reflection: direct + copy at -3 dB, +8 ms (comb) | delay finder picks the direct arrival, not the reflection; comb nulls flagged as low-coherence/not EQ-able | ±0.05 ms |
| S17 | Reference taken through processing (ref = post-EQ copy) | TF shows ~flat; documented as user error; help text warns ref must be pre-DUT | qualitative |
## 2. Calibration file import
Reference file `/workspace/will-gear/ecm8000-cal.csv`: comma-separated with header `Hz,dB,Phase`, then 124 rows. Frequencies run from 20 Hz to 21999 Hz in strictly increasing order, at a log-ish spacing of about 1/10 octave with extra points around 300–380 Hz. dB runs from -4.92 to +1.02. Phase is in degrees. There is no sensitivity line. A tab-separated REW-style copy `ecm8000-cal-rew.txt` uses a `*` comment header, and that header says "Sign convention unverified". **Open question for Vega/Will:** are the dB values the mic's response (to subtract) or the correction (to add)?
| ID | Case | Expected |
|---|---|---|
| C1 | Valid ECM8000 CSV | imports; curve plotted; 124 points |
| C2 | REW .txt (tab-separated, `*` comments) | imports identically to C1 |
| C3 | Header/comment lines (`"`, `*`, `#`, text header) | skipped |
| C4 | Sensitivity line (e.g. `Sens Factor =-1.5dB`, miniDSP-style `"Sens Factor =…dB, SERNO: …"`) | parsed and applied, or ignored with a notice (document which) |
| C5 | Sparse points (5 points) | interpolated on a log-frequency scale, not stepped |
| C6 | Extra/dense points, unsorted, duplicate frequencies | sorted/deduplicated, or rejected with a clear error |
| C7 | Data outside the measurement range (<20 Hz, >fs/2) | clamped or extrapolated in a documented way |
| C8 | Malformed: empty file, binary, non-numeric, missing column, NaN, decimal comma, BOM, CRLF | clear error and no crash; the previous cal is kept |
| C9 | 2-column file (no phase) | accepted |
| C10 | **Curve actually applied:** measure a flat loopback with and without the cal | difference = cal curve (sign per the convention) ±0.1 dB at the cal points |
| C12 | **Sign trap:** load the cal with inverted sign, measure a flat loopback | result deviates by 2× cal (documented); app shows convention in UI ("mic response, subtracted") |
| C13 | SPL calibration with acoustic calibrator (94/114 dB @1 kHz) | stored offset, gain at cal time, date; warns if interface gain changes afterward; weighting (A/C/Z) labeled, no fixed-offset conversion |
| C11 | Cal per input channel; persists after relaunch; can be removed | ✔ |

## 3. Guided workflows (placeholders — Vega to fill in)
For each of GW1–GW3 (names TBD: e.g. delay alignment, sub/main alignment, EQ):
| Step | Check |
|---|---|
| Start | Entry point clear; prerequisites (device, cal, channels) checked before starting |
| Guidance | Each step's instruction is clear; progress indicator; back/cancel works; levels checked |
| Result | Numeric result (delay/polarity/EQ) matches `reference_tf.py` within the §1 tolerances; can be saved/exported |
| Errors | No signal, clipping, low coherence, device lost mid-step, user cancels → recoverable message, no hang |
- GW1: `<TBD by Vega>` — steps / expected result
- GW2: `<TBD by Vega>`
- GW3: `<TBD by Vega>`

## 4. Device setup — Behringer UMC404HD (Will's Mac, permission required)
- D1 Device enumerated by name; 4 in / 4 out shown.
- D2 Input mapping: ref = In X, measurement = In Y; swapping the two is detected or reflected in the results; each of inputs 1–4 is selectable.
- D3 Output mapping: stimulus goes to the chosen out (1/2, 3/4); nothing leaks to other outputs.
- D4 Sample rates 44.1 / 48 / 96 kHz: app follows or sets the device rate; delay in ms stays correct when the rate changes; no pitch or resampling errors.
- D5 Loopback latency is stable over 10 runs (spread ≤ 1 sample).
- D6 Hot-unplug during an idle period, during measurement and during a guided workflow → no crash; clear message; recovers when the device is plugged back in.
- D7 Sharing the device with other apps (SuperRack Performer, Music/Safari playback): no exclusive-mode lockout, and no rate change forced on other apps without a warning.
- D8 Built-in mic/speakers as a fallback device; Aggregate Device behavior.
- D9 Microphone permission prompt (TCC) on first launch; behavior when permission is denied.


## 4b. Field scenarios (Will's Mac + UMC404HD + real speakers/room; permission required; realistic but safe levels)
Run after §1/§4 pass. Use the ECM8000 + cal; log mic positions, temperature, and room state. Cross-check each result against REW/reference_tf from the same capture.
| ID | Scenario | Setup | Expected app behavior |
|---|---|---|---|
| F1 | Noisy room | pink noise at ~10 dB and ~0 dB above ambient (HVAC, talking, music playback) | coherence drops visibly; low-coh regions masked/blanked; averaging (8→16) improves stability; no confident phase in noise; RTA vs TF distinction clear |
| F2 | Low coherence diagnosis | mic far off-axis / behind speaker; level too low; wind/handling (fan on mic) | app flags low validity and suggests cause (level, delay not set, noise); guided workflow refuses to compute a result or warns |
| F3 | Delay not set / drifting | start TF without delay finder; then move mic 3 ft during capture | phase wraps & coherence collapse until delay set; tracker (if present) follows movement (≈0.889 ms/ft); finder sign: + = mic later than ref |
| F4 | Strong reflections | mic near wall/table (floor bounce), or hard room | delay finder locks to direct sound, not reflection; time-window/smoothing options change result as documented; HF combing not offered as EQ target |
| F5 | Sub/main alignment | solo main, capture; solo sub, capture (same ref, no re-find); then combined | crossover region phase-slope comparison; app proposes delay and polarity options incl. ½-cycle+INV alternative; summed trace ≥ either alone ±1 dB in XO band; repeat at 3 positions and report spread |
| F6 | Delay tower / fill alignment (simulate with second speaker 20–40 ft downstream) | measure main and fill separately at transition seat | geometric delay suggestion from distance & temperature; measured result within ±0.5 ms of reference; option to add precedence offset (e.g. +2–5 ms); recheck after temp change note (~0.1%/°F) |
| F7 | Wrong channel / mispatch | stimulus routed to out 3/4 while app expects 1/2; ref and meas inputs swapped; mic on wrong input | app detects no signal / unexpected level / negative delay and states the likely mispatch instead of showing garbage |
| F8 | Wrong polarity | reverse one speaker's wiring (or invert in DSP) | polarity flag shown in solo measurement and in sub/main workflow; not "fixed" by EQ |
| F9 | Clipping / overload | drive interface input into clip; phantom accidentally off/on for ECM8000 | clip indicator, measurement marked invalid; no phantom → "no signal" guidance |
| F10 | Multi-position averaging | 5 seats, captured and averaged | raw traces kept; average method labeled; outlier seats still visible; export includes metadata (time, position, SR, cal, averages) |
| F11 | Sample-rate change by another app mid-measurement (SuperRack/Audio MIDI Setup) | change device SR during TF | app detects, warns, recalculates ms correctly or stops; no silent wrong delays |
| F12 | Interference/ground hum | laptop on charger vs battery, LED/lighting nearby | hum/buzz visible as narrowband RTA lines; app doesn't misreport as system response; note for user |
| F13 | Save/recover under failure | quit/crash app (force quit) and unplug device mid-workflow | captured traces & settings preserved; workflow resumable or cleanly restarted |

## 5. Stability and performance
- P1 Continuous RTA + TF for 2 h: no memory growth over 10%, no dropouts, UI ≥ 30 fps.
- P2 CPU at 48 / 96 kHz with the largest FFT size (Activity Monitor; Instruments Time Profiler).
- P3 Start/stop measurement 100 times; change settings quickly during capture.
- P4 Sleep/wake while running; switching display; launch with no device present.
- P5 Crash logs (~/Library/Logs/DiagnosticReports) checked after each session.
- P6 Signing/notarization: launches with no Gatekeeper warning (see the macos-plugin-sign-notarize skill).

## 6. Usability (from Iris's audio-plugin-ui-design-review skill, adapted)
1. Hierarchy: the main view (TF/RTA/delay) and Start/Stop are obvious within 2 s; controls are grouped by workflow.
2. Readability: text ≥ 11–12 px (values ≥ 13 px); contrast ≥ 4.5:1 for text and ≥ 3:1 for traces and indicators; tested at 50% brightness and in a dark room.
3. Dark stage: no large white areas; traces and readouts readable from 1–2 m at FOH.
4. Color safety: never red/green alone (e.g. coherence or pass/fail); simulate deuteranopia/protanopia.
5. Interaction: cursor readout on hover; double-click resets; fine-adjust modifier; text entry for delay and EQ; meters at 30–60 fps.
6. Scaling: resizable window; crisp on Retina; no clipped text at small window sizes.
7. States: idle, measuring, paused, error, low coherence and clipping are all distinct.
8. Brand consistency with the agreed tokens.
9. Output: report issues by severity (blocker/major/minor) with a screenshot and a concrete fix.

## 7. Results
| ID | Date/time (ET) | Build | SR | Result (measured vs expected) | Pass/Fail | Notes | Will's permission (who/when) |
|---|---|---|---|---|---|---|---|
| S1 | | | | | | | n/a (box) |
| … | | | | | | | |

## Missing inputs
- Vega: does build #1 have coherence masking, delay tracker, time windowing, multi-trace averaging, polarity/½-cycle suggestions? (needed for §4b expectations)
- Will: a second speaker/sub available for F5/F6, and a room he's OK making noise in.
- Vega: build #1 feature spec, GW1–GW3 contents, supported cal formats, FFT/averaging defaults, export format (needed to compare against reference_tf CSV).
- Cal sign convention (see §2).
- Will's permission for each Mac session.
