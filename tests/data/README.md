# Synthetic calibration QA fixtures

`synthetic-mic-cal.csv` is the canonical calibration fixture from #46. It
contains 77 generated points from 5 Hz to 30 kHz, magnitude and phase, and a
Sensitivity Factor of -2.10 dB that is metadata only. No fixture represents a
real microphone.

The REW `.txt` and `.frd` variants contain the same sorted points. The
`SIGNFLIPPED.csv` variant negates both magnitude and phase to test an incorrect
calibration convention. The response is subtracted: corrected = measured - cal.

Calibration expectations for the QA checklist (#41) and automation (#43):

| QA IDs | Synthetic expectation |
| --- | --- |
| C1, C5, C7 | 77 points; endpoints -2.234 dB at 5 Hz and -2.468 dB at 30 kHz; log-frequency interpolation and endpoint clamping |
| C2, C2b | REW and FRD magnitude/phase arrays equal the CSV |
| C4 | Sensitivity metadata does not alter correction or the applied TF |
| C10 | Flat TF correction: +0.994 dB at 20 Hz, +1.270633 dB at 20361.5 Hz, +1.574083 dB at 21999 Hz |
| C12 | Sign-flipped correction of the synthetic response: -2.599217 dB at the FFT bin nearest 20.5 kHz, -3.148174 dB at the bin nearest 21999 Hz (48 kHz, FFT size 16384) |
| C14 | Correct calibration removes the generated magnitude and phase response |

These replace the old 124-point calibration and its 6 dB sign-trap threshold.
The calibration cases in `test_core.cpp` carry the corresponding Part A tags.
The golden-signal tests (#39) and runtime room simulation (#44) have no
calibration-file dependency and require no calibration expectation changes.
Updates live on the task branch; the other PR branches are not rebased.
