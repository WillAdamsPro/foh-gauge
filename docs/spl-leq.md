# Indicative SPL / Leq

The SPL / Leq panel is available in RTA/Show and the panel menu. Calibrate the
selected microphone input in Setup first. A, C and Z readings use the existing
SPL calibration and its relative microphone sensitivity correction.

The worker applies bilinear-transform A/C weighting filters normalized at 1 kHz;
Z is unweighted. Energy uses the same peak-referenced dBFS convention as SplCal.
Live SPL uses the last completed 100 ms bucket. Leq averages linear energy,
with rolling 60 s and 900 s windows stored in 100 ms buckets. The oldest partial
bucket is prorated, so changes inside that bucket are approximate. Before a
window fills, its denominator is the measured duration, labelled as partial.
Session Leq includes all processed samples since input or calibration reset.
Pause excludes samples; input/sample-rate changes and calibration changes reset
all windows. Readouts are withheld without calibration or a current input.
Frequency response correction uses the existing 1 kHz level convention; this
is an indicative meter, not a certified sound level meter.

Warnings always use calibrated A weighting, even with C or Z selected:

- WHO: warn at 100 dBA LAeq over 15 minutes (elapsed time before it fills).
  [WHO standard](https://www.who.int/publications/i/item/9789240043114).
- NIOSH: warn when measured session exposure reaches 100% of 85 dBA for
  eight hours, with a 3 dB exchange rate. Unmeasured exposure is excluded.
  [NIOSH exposure limits](https://www.cdc.gov/niosh/noise/prevent/understand.html).

All filtering, history and snapshot work runs on the analysis worker. The audio
callback continues to transfer samples through the existing SPSC rings.
