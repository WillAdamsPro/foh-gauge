# W4 tuning and W5 pre-doors acceptance walk-through

Use only a bench loopback and synthetic signals. No real calibration files or
location data are needed. Item 33's Store / manual Apply / Verify frame is used
for both workflows. FOH Gauge never writes DSP settings.

## Bench setup

1. Connect a line output to two line inputs (split loopback), assigned as mic and
   reference in Setup. Disable phantom power on these line inputs. Start with
   generator gain low; raise it only enough to get clean input levels without
   clipping. Keep loudspeaker outputs muted/disconnected for the bench test.
2. Select the same device and sample rate for both inputs. Use pink noise and W1
   to find and lock the loopback reference delay. Confirm a stable, near-flat TF
   and high coherence. A split loopback cannot validate acoustic SPL, speaker
   coverage, or sub/fill alignment.
3. Store a named tuning reference trace. Keep the input gains, routing, reference
   delay and cables unchanged during each before/after comparison.

## W4

1. Select **W4: Tuning**. Confirm the order is **SPL cal**, **Time alignment**,
   **Tuning order**, **Store**, **Apply**, **Verify**. Read the calibration step:
   it directs the user to SPL Cal/Setup, identifies 94/114 dB or known-level
   calibration and warns that uncalibrated readings are dBFS. On the bench,
   leave SPL uncalibrated; acknowledge only the instructional check. Actual
   acoustic calibration needs a microphone and a known acoustic level.
2. Read the alignment step: W1, W2 and W3 (if fills are present), with Verify,
   precede EQ. For this split loopback, only W1 is applicable. Confirm checklist
   acknowledgements advance without starting the generator, locking a new delay,
   applying calibration or changing routing.
3. Read the tuning order: mains, mains + subs, fills/full system; representative
   positions, aiming/relative levels before broad EQ, one change at a time.
   Confirm **Back** and the acknowledgements work and all text/buttons fit in a
   260 x 660 guide panel, especially the W5 multiline list.
4. At Store, click **Measure before change** and wait for a settled capture.
   Apply must require that baseline. With the mic and reference paths unchanged,
   manually reduce only the bench mic path by 3 dB in an external test processor
   (or a mic-only hardware attenuator); never change FOH input gain/routing or
   the common generator level for this comparison.
5. Confirm **I entered the change**, then **Re-measure to verify**. Expect about
   -3 dB mean change, high coherence and the before/after overlays. Pass/Fail
   remain disabled until a settled after capture and a nonempty note. Record
   a synthetic note and Pass. Return through W4 for another change if needed.

## W5 (five-minute budget)

1. **0-1 min:** inspect the signal-path checklist. Confirm devices, mic/ref
   channels, rate, gain/calibration, preset, mutes, routing and signal/clipping
   meters. Speaker-by-speaker checks require the real system later.
2. **1-3 min:** compare the live loopback TF with the stored tuning trace at the
   same reference delay. Confirm coherence and settled response; read the
   crossover and main/fill listening checks as acoustic follow-up. Stop the
   generator after measurements using its normal control; acknowledgement
   buttons do not switch it on or off.
3. **3-5 min:** read the full symptom-to-cause list: no signal, low coherence,
   LF dip, comb filtering, harsh/clipped sound and clicks/dropouts. A listed
   cause is a hypothesis to check, not an automatic diagnosis. Confirm it says
   to change one cause at a time and mark unresolved faults Fail.
4. Finish Store / Apply / Verify within the budget when healthy. Use brief pink noise for the frame captures and stop it after Verify. Store the
   settled before trace, leave the external DSP unchanged, click **DSP state
   confirmed**, then re-measure. Expect approximately 0 dB change. Add a note
   and Pass. If a correction is needed, Store before changing it manually and
   Verify afterward. Fault isolation may exceed five minutes; the budget is
   not permission to declare unresolved faults ready.
5. Repeat with signal missing or too weak: an unsettled capture must not enable
   Pass/Fail. Restore signal and repeat. Change reference delay between before
   and after: Verify must send the user back to Store for a new comparison.

## Verification status

Automated app tests use synthetic coherent TF traces, with no audio device,
model timers or preference writes. They cover W4/W5 manual advancement and
readable layout at the minimum panel size, unchanged generator/reference delay,
settled baseline gating, fresh Verify, zero/nonzero comparisons, verdict notes,
unsettled/low-coherence rejection and invalidation after reference-delay changes.

This is the repeatable hardware acceptance procedure. Physical loopback,
acoustic calibration and loudspeaker listening checks have not been performed
by the coding agent; synthetic tests do not establish those results.

Local validation (Apple Silicon Mac, Release, macOS deployment target 26.0):

- Core-only build and CTest: 119/119 passed.
- App and test build using Command Line Tools / Unix Makefiles (Xcode is not
  installed on this Mac); combined CTest: 164/164 passed.
- Focused synthetic guide suite: 6 cases, 354 assertions passed.
- App executable is arm64; bundle minimum system version is 26.0.
- Public string scan and `git diff --check` passed. No audio-thread code changed.
