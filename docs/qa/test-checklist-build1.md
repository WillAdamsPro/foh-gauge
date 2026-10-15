# FOH Gauge — Test Checklist, Build #1 (FINAL, due Oct 22 2026; build ~Oct 25)
Owner: QA - Echo · Builder: Vega · Spec: Vega 2026-10-13 · Backups: `test-checklist-build1.md.bak-20261013` (pre-final), `.bak-20261013b` (pre-decisions), `.bak-20261013-192520` (pre-Mac/gear) · **Updated for Vega's binding decisions D1–D8 + D9–D14 confirmations; test Mac + field gear (Oct 13)**

> **GATING:** Every session on Will's Mac (install, un-quarantine, launch, device access, audio I/O) needs **Will's explicit approval for that session, relayed through Atlas**. Approval doesn't carry over between sessions. Log who approved it and when in §R. Part A runs on the Linux box and needs no approval.

**Build under test:** zipped `.app` from GitHub Actions artifact **FOH-Gauge-macOS-arm64** on `master` (record the run ID and commit SHA). Standalone, arm64, unsigned.
**Hardware:** UMC404HD @ 48 kHz · Out1 → console · Out2 → TRS loopback → In2 (reference) · ECM8000 on In1 (+48 V).
**Test Mac:** MacBook Pro M1 Max, 64 GB RAM, **macOS 26.6.2**. App minimum: **macOS 26.0**.
**Field speakers:** spare **QSC K12.2** tops (available any time) · pair of **QSC KS118** subs. **Loud tests after hours only.**
**W2 gear:** KS118 (sub) against a K12.2 (mains). **W3 gear:** second K12.2 as the fill.
**Defaults under test:** TF 16384-pt FFT @ 48 kHz, Hann, Welch 50% overlap, 16 averages (+ infinite/exponential); smoothing 1/3, 1/6, 1/12, 1/24 oct; core blanking default 0, **UI default 0.5**, configurable (D6); traffic light **green ≥0.9 / amber 0.6 ≤ c <0.9 / red <0.6** (D2).

Feature tags: **W1** setup/ref delay · **W2** sub→mains · **W3** fills→mains · **TF** · **DF** delay finder · **POL** · **RTA** · **CAL** · **TR** traces · **GEN** · **WS** workspaces/themes · **CAP** settle-based capture.

---
## Part A — Synthetic / automated (Linux box, core library)
**Where:** `core/` (`fohcore.h`) tested by Catch2 in `tests/test_core.cpp` (`fohcore_tests`, run by ctest; data in `tests/data/`, `FOH_TEST_DATA`). Build: `cmake -S . -B build-core -G Ninja && cmake --build build-core && ctest --test-dir build-core`.
"Existing" = a TEST_CASE already in `test_core.cpp` (master @ 7cd8876). "NEW" = a case QA proposes Vega add. Cross-check column = box reference run with `/workspace/audio-qa`.
**Stimuli:** `audio-qa/venv/bin/python audio-qa/gen_stimuli.py <out> 48000` (also 44100/96000). Reference: `reference_tf.py ref.wav meas.wav [--nfft N] --csv`; `latency.py`; `null_test.py`.
**Tolerances:** TF mag ±0.5 dB; phase ±5° where coh >0.9; delay ±0.05 ms or ±1 sample (whichever is larger; sub-sample cases ±0.1 sample); RTA band ±0.5 dB.

### A1. Transfer function (TF; feeds W1–W3)
| ID | Test | Expected | Core test | Cross-check |
|---|---|---|---|---|
| S1 | ref vs identical copy (+gain, comp. delay) | 0 dB, 0°, coh ≈1 | Existing "pure gain + compensated delay" | reference_tf `known_reference` ×2 |
| S2 | +6 dB PEQ 1 kHz Q1 | +6.00 @1k; ≈+1.9 @500/2k; 0° @1k | NEW (biquad DUT exists in plain-xcorr test; add mag/phase asserts) | `known_peq1k_+6dB_Q1` |
| S3 | 2nd-order HPF 100 Hz | −3.0 @100; ≈−8.6 @63; ≈−20.3 @31.5; +90° @100 | Existing LPF case; NEW HPF mirror | `known_hpf100_2nd` |
| S6 | polarity-inverted DUT | 180°, coh 1, DF `inverted=true` | Existing "Polarity flip" | latency.py → polarity=inverted |
| S7 | gain −6 / +12 dB | flat −6 / +12 | Existing (gain) + NEW +12 | — |
| S8 | uncorrelated noise SNR 0/−10 dB | coh ≈0.5 / ≈0.09; mag still ≈0 dB in the average | Existing "Coherence drops…" | `known_lowcoh_snr0dB` (coh 0.41 @1k with 65k nfft), `known_lowcoh_snr-10dB` (0.07) |
| S12 | S1–S3 at 44.1 / 96 kHz | same within tol (ms scales) | NEW (parametrize fs) | gen_stimuli at 44100/96000 |
| T1 | **Defaults:** `TransferFunction()` → N=16384, fs=48000, Hann, 50% overlap, avgN=16 | reported in `TFResult.fftSize`, `.averages` | NEW | — |
| T2 | **Averaging modes:** 16-avg Welch (fixed), infinite = cumulative mean, exponential tracks a step change | infinite variance ↓ with frames; exp. reaches new level within ~N frames | Existing "Averaging: infinite… exponential" + NEW fixed-16 window | — |
| T3 | **Smoothing 1/3, 1/6, 1/12, 1/24** | flat stays flat (±0.01 dB); ripple reduced; resolution 1/24 > 1/12 > 1/6 > 1/3 | Existing (×2 smoothing cases) – add 1/3 & 1/6 to ordering assert | — |
| T4 | **Core blanking default 0** (`TransferFunction` ctor) | threshold 0 → `blanked` all 0; data unchanged | Existing "Coherence blanking flags noisy bins only" | — |
| T4b | **UI default 0.5 (D6)**: fresh launch / reset prefs → TF panel threshold reads 0.50 and is passed to core; bins with coh 0.49 blanked, 0.50 not blanked (blank if coh < thr) | app check → Part B H13 | — |
| T5 | **Blanking configurable** | set 0.6/0.9 → only bins with coh < thr flagged; `magDb/H` identical to unblanked (data kept); clamps <0→0, >1→1 | Existing (partial) + NEW clamp & data-unchanged asserts | `known_lowcoh_snr0dB` |
| T6 | **Traffic light (D2, D3, D11)**: 1.0→green, **0.9→green**, **0.8999→amber**, 0.75→amber, **0.6→amber**, **0.5999→red**, 0.0→red; **NaN→red** (Vega confirmed) | exact, no tolerance | **NEW core test** (Orion moves classifier into core) | — |
| T6b | App guide indicator uses the core classifier (no duplicate thresholds in app code) | `rg "0\.9\|0\.6" app/Source` shows no colour thresholds | code check | — |
| S17 | ref taken post-DUT | TF ≈ flat (user error) — help text warns ref must be pre-DUT | doc/UI check only | — |

### A2. Delay finder (DF; W1 step 5, W2 step 3, W3 step 3)
| ID | Test | Expected | Core test | Cross-check |
|---|---|---|---|---|
| S4 | fractional 4.80 ms (230.4 smp) | 230.4 ±0.1 smp | Existing "integer and fractional" | `known_delay_4.80ms_frac`: latency.py |
| S5 | integer 230 smp | 4.7917 ms ±1 smp | Existing | `known_delay_230samp` |
| D-T3 | **Top-3 arrivals**, direct + 2 reflections in noise | 3 arrivals sorted, direct = #1, `relLevelDb` correct ±1 dB, `distanceM` = ms×0.343 | Existing "top-3 arrivals" | — |
| S16 | **Reflection stimulus** −3 dB at +8 ms (boundary) | #1 = 2.00 ms ±0.05; #2 = 10.00 ms ±0.05. Flag not asserted (on the D4 boundary) | NEW | `known_reflection_d2ms_r-3dB_+8ms` → 2.0000 ms ✔ |
| S16a | Reflection **−2.5 dB** at +8 ms | #1 = 2.00 ms; **two-spike flag = true** (D14 — in build #1) | NEW | `known_reflection_d2ms_r-2.5dB_+8ms` → 2.0000 ms ✔ |
| S16b | Reflection **−3.5 dB** at +8 ms | #1 = 2.00 ms; **flag = false** | NEW | `known_reflection_d2ms_r-3.5dB_+8ms` → 2.0000 ms ✔ |
| D-2S | **Two-spike flag rule (D4, D9)**, pure function on arrivals: flag ⇔ (#1 dB − #2 dB) ≤ 3 dB **and** \|Δt\| > 0.5 ms. Level = **real arrival level**, measured **against the reference spectrum by default** (`LevelMode::RefWhitened`, impulse-response estimate); **unweighted** envelope available as an option (`LevelMode::Unweighted`); PHAT for timing only (D9, revised). Test both modes. Cases: (2.9 dB, 2 ms)→**true**; (3.1 dB, 2 ms)→**false**; (0.5 dB, 0.4 ms)→**false**; (0.5 dB, 0.6 ms)→**true**; (0.5 dB, 1.5 ms)→true | exact | **NEW core test** (flag field on `DelayResult`) | — |
| D-2Sa | End-to-end: 3.00 + 4.50 ms at −0.5 dB | **flag = true**; #1 = 3.00 ms ±0.05; both in top 3; confidence < clean case | NEW | `known_twospike_3ms_4.5ms_-0.5dB` → 3.0001 ms |
| D-2Sb | End-to-end level boundary: 3 ms + 5 ms at −2.9 / −3.1 dB (arrival level, D9) | −2.9 → true, −3.1 → false | NEW | `known_twospike_3ms_+2ms_-{2.9,3.1}dB` |
| D-2Sc | End-to-end spacing boundary: 3 ms + (3.4 / 3.6 ms) at −0.5 dB | 0.4 ms → false, 0.6 ms → true | NEW | `known_twospike_3ms_+{0.4,0.6}ms_-0.5dB` (3.0003 / 3.0002 ms) |
| D-CF | **Confidence score**: a two-spike flag multiplies confidence by **0.75 on a log scale**; expected order clean > reflection > two-spike > SNR −10 dB | monotonic ordering; range 0–1; flagged result = unflagged × 0.75 (log-mapped) | NEW | above files |
| S15/D-SUB | **Band-limited sub (30–120 Hz), 3 s window**: sub filtered ref vs same delayed 12.5 ms | 12.5 ms ±0.1 using a 3 s window; finder uses 3 s for subs (W2) | NEW | `known_sub_bp30-120_0ms` vs `known_sub_bp30-120_12.5ms` → 12.4994 ms ✔ |
| D-SUBGD | **W2 sub-delay trap (D7)**: mains TF = `known_sub_bp30-120_0ms`-style flat main vs sub `known_sub_bp30-120_12.5ms`, both against `known_sub_reference` | **primary = phase-offset delay in XO band ≈ 12.5 ms ±0.25 ms**; alternatives shown at primary ± 1 period of the XO frequency used (e.g. f_xo 80 Hz → 12.5 ± 12.5 ms = 0 / 25 ms), each ±0.25 ms; raw peak from plain cross-correlation (**≈19.7 ms**, pol −; not 20.5) shown only as "hint", never inserted as primary. Helper (`subDelayFromPhase`) for an **80 Hz** crossover returns **12.5 ms** primary with alternatives **0 ms and 25 ms** | NEW (extends `phaseOffset` test) | plain xcorr → ≈19.7 ms pol −; filtered pair → 12.4994 ms |
| S13 | long delays 50/200/500 ms, edge of window | found, or clear out-of-range | NEW | — |
| D-PX | PHAT vs plain xcorr agree on white noise / biquad | ±0.1 smp | Existing "plain cross-correlation" | — |
| D-ERR | **findDelay error, never throw**: empty input, mismatched/too-short sizes (<64), zero-length ref or meas | `error` set, `ok()==false`, no exception, no UB (sanitizer clean) | NEW core test | — |
| D-SGN | **Sign (D1)**: meas = ref delayed 4.80 ms → +4.80 ms; ref delayed relative to meas (swap files) → −4.80 ms | sign exact, ±0.05 ms | NEW (assert sign in existing delay test) | latency.py swap |
| D-INS | **Insert = full sub-sample (D1, D10)**: arrival 230.4 smp → analysis delay = 230.4 smp (not 230); TF phase @10 kHz within ±5° of 0 (rounding to 230 would leave 0.4 smp ≈ 30° at 10 kHz, so a rounded insert fails). Reference delay becomes fractional through **linear phase** (Vega D10) | fractional `setReferenceDelay` via linear phase | **NEW core** | `known_delay_4.80ms_frac` |
| D-DSP | **Console display rounded to 0.01 ms (D1, D12)**: 230.4 smp → "4.80 ms"; 230.0 → "4.79 ms"; 4.8049 ms → "4.80"; 4.8050 → "4.81" (**round-half-up**, Vega confirmed) | string exact | NEW (formatter) | — |
| D-1S | "Find again" / "Insert delay" are one-shot: result doesn't update without a click | NEW (app/controller) | — |

### A3. Polarity + virtual sum (POL; W2 steps 3–4)
| ID | Test | Expected | Core test |
|---|---|---|---|
| S14 | LR4 sub (100 Hz) vs main, 5 ms + INV vs 10 ms normal | virtual sum shows both options; neither auto-picked (no auto recommendation in build #1) | Existing "Guide: sub-to-mains phase offset and virtual sum" + NEW INV case |
| P-VS | virtualSum(main, main) = +6.02 dB; virtualSum(main, inverted main) ≤ −40 dB | ±0.01 dB | Existing (+6.02) + NEW (null) |
| P-NA | No API/UI path emits a polarity "recommendation" | code/UI check | — |

### A4. Calibration (CAL)
Convention: the file **is the mic response** and is **subtracted** (REW). Corrected = measured − cal(f). ECM8000 file rolls off ≈ −3 to −5 dB above 18 kHz, so **with cal the top end reads higher by ≈3–5 dB** (e.g. +4.92 dB at 21999 Hz, +4.01 at 20361.5).
| ID | Test | Expected | Core test |
|---|---|---|---|
| C1 | `.csv` Hz,dB,Phase — `will-gear/ecm8000-cal.csv` | 124 points, hasPhase, ends clamp | Existing "Will's ECM8000 CSV" |
| C2 | `.txt` REW (tab, `*` comments) — `will-gear/ecm8000-cal-rew.txt` | identical arrays to C1 | NEW (load both, compare) |
| C2b | `.frd` (`;` comments, space-sep) — `ecm8000-cal.frd` from gen_stimuli | identical to C1 | NEW (inline frd parsing is Existing) |
| C3 | header/comment lines (`"`, `*`, `#`, `;`, text header, BOM, CRLF, `;`/`,`/tab separators) | skipped | Existing "tolerant parsing" |
| C4 | **Sens Factor (D8)**: `"Sens Factor =-1.5dB, SERNO: 123"` | `sensitivityOffsetDb == −1.5` (Existing); **NEW**: `apply()`/`correctionDb()` output identical with and without the Sens line (offset not applied, ±0.000 dB); cal info panel shows "Sens Factor −1.5 dB (not applied)" | Existing + NEW + Part B H8 |
| C5 | sparse points | log-frequency interpolation | Existing (midpoint assert) |
| C6 | unsorted / duplicate freqs | sorted/deduped or clear error | NEW |
| C7 | data outside range | clamped at ends | Existing |
| C8 | malformed (empty, binary, non-numeric, NaN, decimal comma) | `ok()==false`, clear error, previous cal kept | Existing (text) + NEW (others) |
| C9 | 2-column (no phase) | accepted, `hasPhase=false`, magnitude only | Existing |
| C10 | **Applied with correct sign**: flat TF (0 dB) + ECM8000 cal | corrected = −cal(f) ±0.01 dB: **+4.92 dB @21999 Hz, +4.01 @20361.5**, ≈+0.07 @20 Hz; and TF = mic response → 0 dB | Existing ("removes the mic response", `correctionDb = −dbAt`) + NEW explicit top-end-rise assert |
| C12 | **Sign trap**: load `ecm8000-cal_SIGNFLIPPED.csv`, apply to TF equal to the real mic response | result is ≈2× cal (≈−9.8 dB @21999 Hz) instead of 0 → test asserts the error is ≥ 6 dB at >20 kHz, i.e. a flipped file is **detectably wrong**; UI labels the convention "mic response, subtracted" | NEW |
| C11 | per-input cal, persists, removable | app-level → Part B H8 | — |
| C14 | cal phase applied (if hasPhase) | phase at 1 kHz corrected by −phaseAt | Existing (phase=0 assert) |

### A5. RTA, generators, traces, capture, workspaces
| ID | Test | Expected | Core test / method |
|---|---|---|---|
| S10 | RTA 1 kHz sine 0 dBFS, 1/3 & 1/6 | ≈0 dBFS in 1 kHz band, ≤1-band leakage | Existing |
| S9 | RTA pink −12 dBFS RMS | flat bands (pink): **±0.5 dB at ≥200 Hz, ±1.5 dB below 200 Hz**; band centres IEC base-10 | NEW (use `pink_-12dBrms`) |
| R-FS | RTA reads dBFS only — no SPL/weighting labels anywhere | code/UI check | — |
| G1 | pink noise slope −3 dB/oct | ±0.5 dB/oct | Existing |
| G2 | log sweep bounded, expected length, plays only (no deconvolution/IR output) | Existing + check no sweep-IR feature exposed | Existing |
| G3 | generator gain/reset thread-safe | Existing | Existing |
| TR1 | **8-trace limit**: 8 stored, 9th refused with message; delete frees a slot | Existing "Trace store caps at 8" |
| TR2 | show/hide toggles `visible` without altering data; delete reorders correctly | NEW |
| CAP0 | **Settle rule (D5, D13 — in build #1)**, pure function: settled ⇔ RMS of ΔdB over bins with coh ≥ 0.6 in 100 Hz–10 kHz < 0.5 dB between successive averages, for **3 consecutive** updates, **and at least 25% of the 100 Hz–10 kHz bins coherent** (coh ≥ 0.6; fraction configurable, 0 disables the check). Test both: 24% coherent → never settles (times out, `unsettled`); with the fraction set to 0 the same input settles. Cases: ΔRMS sequence 0.4,0.4,0.4 → settled at update 3; 0.4,0.4,0.6,0.4,0.4,0.4 → counter resets, settled at update 6; 0.5,0.5,0.5 → **not** settled (strict <); bins with coh 0.5999 or outside 100 Hz–10 kHz with huge Δ → ignored | exact | **NEW core test** (Orion) |
| CAP1 | **Stable signal settles**: `known_reference` vs `known_peq1k_+6dB_Q1`, 16-avg | settled = true well before 10 s; no warning; result matches S2 | **NEW core** |
| CAP2 | **Drifting signal times out**: `known_reference` vs `known_drift_2to4ms_gain3dB` (looped) | not settled; proceeds at **10.0 s ±0.5 s** with `unsettled` warning; result still returned | **NEW core** |
| CAP3 | **Low coherence**: `known_lowcoh_snr-10dB` and silence on meas | ≤10 s, `unsettled` warning (or empty coherent-bin set → warning, no divide-by-zero / NaN); no hang | **NEW core** |
| CAP4 | Infinite / exponential mode: same rule, still terminates ≤10 s | as CAP1–3 | NEW core |
| RT1–4 | RT safety (no alloc 16..2048, scratch reuse, ring buffer, denormals) | Existing | Existing |
| WS1 | Workspace presets Setup / Alignment / Tuning / RTA-Show, Cmd+1..4 | correct panel sets | app-level → Part B H15 |
| WS2 | Tear-off position restore per display; missing display → main display | serialise/restore logic unit-testable if factored out (ask Vega); else Part B | NEW / H16 |
| WS3 | Themes Pro Dark / Sunlight: all `FohTheme.h` tokens defined for both; text contrast ≥4.5:1, traces ≥3:1 | script check of FohTheme.h tokens | NEW (box script) |

**Part A count: 70 items** (A1 16 · A2 20 incl. new D-ERR · A3 3 · A4 14 · A5 17; RT1–4 counted as one row). 8 are NEW core tests per D3 or required API changes (T6, D-2S, D-INS, CAP0–CAP4).

---
## Part B — Hardware smoke test (Will's MacBook Pro M1 Max 64 GB, macOS 26.6.2; app min macOS 26.0; ONE session ≈ 60 min, approval via Atlas required)
Pre-flight (box): Part A green on the same commit. Bring: TRS cable Out2→In2, ECM8000 + stand, XLR to console, `ecm8000-cal.csv`. Speakers: **K12.2** (mains) + **KS118** (W2 sub) + second **K12.2** (W3 fill); spare K12.2 tops available any time; **loud tests after hours only**. Keep levels moderate (console fader down at first).
| # | Step | Pass if | Tags | ~min |
|---|---|---|---|---|
| H1 | Download artifact FOH-Gauge-macOS-arm64 (master), unzip, `xattr -dr com.apple.quarantine "FOH Gauge.app"` or right-click › Open | launches, arm64 (Activity Monitor "Apple"), version/SHA shown | — | 4 |
| H2 | Mic permission prompt on first launch | prompt appears; denying shows clear guidance | — | 2 |
| H3 | W1-1 device: pick UMC404HD @ 48 kHz | 4 in/4 out listed; rate 48 kHz | W1 | 2 |
| H4 | W1-2 channel map: Out1 console, Out2 ref, In2 ref, In1 mic | saved; swapping In1/In2 is visibly wrong (negative/odd delay) then restored | W1 | 3 |
| H5 | W1-3 signal presence both inputs | both meters live; unplugging TRS shows "no reference signal" | W1 | 2 |
| H6 | W1-4 pink noise on Out1+Out2; RTA 1/3 & 1/6 in dBFS | RTA ≈pink-flat on In2; no SPL figures shown | GEN RTA | 3 |
| H7 | **Loopback TF**: TRS In2 as ref, second TRS Out2→In1 (or route mic input to loopback) | ≈0 dB ±0.5, 0° ±5°, coh >0.95 (green) | TF | 4 |
| H8 | Load `ecm8000-cal.csv` on In1; also `.txt` and `.frd` | all accepted; top end reads **higher** with cal (≈+4–5 dB above 20 kHz on loopback); cal info shows Sens Factor (if present) as not applied; remove cal works; persists after relaunch | CAL | 5 |
| H9 | W1-5 Find delay on speaker+mic, then Insert | delay positive (mic later); console value shown to 0.01 ms; phase flattens fully (sub-sample insert); 10 repeats spread ≤1 sample | DF W1 | 4 |
| H10 | Move mic ~1 ft; delay does **not** auto-update; Find again updates by ≈0.89 ms | one-shot only | DF | 2 |
| H11 | Reflection: mic near a table/wall | #1 = direct; two-spike flag only if #2 within 3 dB and >0.5 ms away | DF | 3 |
| H12 | Capture with clean signal / then noise off-axis or level very low | clean: settles, no warning; low-coh / moving mic: proceeds by 10 s with 'unsettled' warning (stopwatch) | CAP | 4 |
| H13 | Averaging 16 ↔ infinite ↔ exponential; smoothing 1/3→1/24; blanking default reads **0.5**; set 0.8 then 0 | blanked areas return at 0 (data kept); traffic light green ≥0.9 / amber / red <0.6 | TF | 4 |
| H14 | W2 sub→mains (05–09): **KS118** (sub) against a **K12.2** (mains); loud after hours only | 3 s window used; primary delay from XO phase offset with ±1-period alternatives, raw peak labeled hint; polarity note + virtual sum normal vs INV; no auto recommendation; summary correct | W2 POL | 8 |
| H15 | W3 fills→mains: second **K12.2** as the fill vs mains K12.2; loud after hours only | fill offset applied at the seam; verify step shows sum | W3 | 5 |
| H16 | Store 8 traces, try 9th; hide/show/delete | 9th refused clearly | TR | 2 |
| H17 | Cmd+1..4 workspaces; tear off a panel, quit/relaunch; unplug external display | positions restore; missing display → main display | WS | 3 |
| H18 | Pro Dark ↔ Sunlight; readable at FOH distance / bright room | both readable; no red/green-only cue | WS | 1 |
| H19 | Hot-unplug UMC404HD during capture, replug | no crash; clear message; recovers | — | 2 |
| H20 | Out-of-scope check: no export, SPL cal, live tracker, user layouts, drag-to-dock visible (or clearly disabled) | none exposed as working features | — | 1 |
| H21 | After session: check `~/Library/Logs/DiagnosticReports` for FOH Gauge crashes; note CPU % at idle/TF | 0 crashes; CPU noted | — | 2 |
**Part B count: 21 items, ≈66 min** (W2 = KS118 vs K12.2; W3 = second K12.2 as fill; loud after hours).

---
## Deferred — not in build #1 (kept, not deleted)
| ID | Item | Reason |
|---|---|---|
| C13 | SPL calibration with acoustic calibrator (94/114 dB), weighting A/C/Z | SPL cal out of scope |
| X1 | CSV / image export of traces; export metadata (old F10 tail, old GW "can be exported") | export out of scope |
| F3b | Live delay tracker following mic movement | live tracker out of scope (one-shot only — tested in H10) |
| L1 | User-saved layouts | out of scope |
| L2 | Drag-to-dock panels | out of scope |
| WIN | Windows build tests | out of scope |
| S11 | Sweep deconvolution / MLS TF equivalence | sweep is play-only in build #1 |
| P-AR | Automatic polarity / ½-cycle recommendation | no auto recommendation in build #1 |
| P6 | Signing / notarization | build is unsigned by design |
| F1–F13 | Full field scenarios (noisy room, mispatch, clip/phantom, multi-seat averaging, SR change by other app, hum, crash-recovery, temperature delay) | post-build-1 field session; parts covered by H5/H11/H12/H19. Gear available (K12.2 tops + KS118 pair); needs a room Will approves; loud after hours |
| D4/D7/D8 | 44.1/96 kHz on hardware, device sharing with SuperRack, aggregate/built-in device | spec fixes 48 kHz; later |
| P1–P4 | 2 h soak, 96 kHz CPU, 100× start/stop, sleep/wake | later stability pass |
| U1–U9 | Full usability review (Iris skill) | later; only H18 quick check now |

## §R Results
| ID | Date/time (ET) | Build (run ID/SHA) | Result vs expected | Pass/Fail | Notes | Will's approval (via Atlas, who/when) |
|---|---|---|---|---|---|---|
| A-all | | | | | | n/a (box) |
| H1 | | | | | | |

## Decisions (Vega, Oct 13) — binding for build #1
| # | Decision | Tests |
|---|---|---|
| D1 | + delay = mic later than ref. Insert applies the full sub-sample value to analysis; console value displayed rounded to 0.01 ms | D-SGN, D-INS, D-DSP, H9 |
| D2 | Coherence ≥0.9 green; 0.6 ≤ c < 0.9 amber; <0.6 red. Boundaries 0.9, 0.6, 0.8999, 0.5999 | T6, T6b, H13 |
| D3 | Traffic-light + settle logic move into core (Orion) | T6, CAP0–4 marked NEW core |
| D4 | Two-spike flag: #2 within 3 dB of #1 and >0.5 ms away | D-2S (2.9/3.1 dB, 0.4/0.6 ms), D-2Sa–c, S16/S16a/S16b, H11 |
| D5 | Settled: RMS ΔdB over coh ≥0.6, 100 Hz–10 kHz bins < 0.5 dB, 3 consecutive updates, ≥25% of those bins coherent (configurable, 0 = off); max 10 s then proceed with 'unsettled' | CAP0–CAP4, H12 |
| D6 | UI default TF blanking 0.5, configurable | T4b, T5, H13 |
| D7 | W2 sub delay from XO-band phase offset, ±1 period alternatives; raw peak = hint | D-SUBGD, S15, H14 |
| D8 | Sens Factor parsed and shown, not applied | C4, H8 |
| D9 | Two-spike flag compares **real arrival level**, against the reference spectrum by default (unweighted optional); PHAT used for timing only; flag → confidence ×0.75 (log scale) | D-2S, D-2Sa–c, S16a/b, H11 |
| D10 | Reference delay becomes fractional through **linear phase** | D-INS, H9 |
| D11 | NaN coherence → **red** | T6 |
| D12 | Display rounds **half up** (e.g. 4.8050 → 4.81) | D-DSP |
| D13 | Settle-based capture **in build #1** | CAP0–CAP4, H12 |
| D14 | Two-spike flag **in build #1** | D-2S, S16a, H11 |

**Notes / follow-ups (not blocking):**
- **N1 — resolved (D9):** two-spike compares **real arrival level** via unweighted envelope; PHAT for timing only. End-to-end S16a/D-2Sb stand as written.
- **N2 — resolved (D10):** reference delay becomes fractional through **linear phase** (D-INS).
- **N3 — resolved (D12):** display rounds **half up** (D-DSP).
- **N4 —** D-SUBGD: the alternative spacing uses the period of the XO frequency the guide reports; record it in the result.
- **N5 — confirmed (D13, D14):** settle-based capture and the two-spike flag are **in build #1**.
