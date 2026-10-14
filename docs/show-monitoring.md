# W7 show-time monitoring

Open Show mode, or the W7 Show Monitoring tab in the RTA/Show preset. Feed
console programme into the reference input, keep the mic at its reference
position, select a stored TF, and click Monitor. The transport must be running.
An older saved layout can still reach W7 through Show mode.

The panel copies the selected trace so deletion or changes to the trace store do
not change an active baseline. Live TF and RTA reuse the existing analysis
pipeline. Monitoring publishes snapshots and updates charts once per second.
Hidden charts do not render. Stop monitoring or destroy the panel to release
output suppression; the previous generator is never restarted automatically.

All generated output (pink noise, sweep, chime and connection tests) is suppressed
while monitoring. The audio callback reads one lock-free atomic flag, skips the
playback loop and continues normal input capture. No new allocation, lock or I/O
runs in the audio callback. TF comparison, chart vectors and trace copies run on
the message thread; TF/RTA processing stays on the analysis worker.

Drift is RMS magnitude difference over equally spaced 1/12-octave frequencies
from 100 Hz to 10 kHz. Both traces must have coherence >=0.6, unblanked finite
magnitudes and matching FFT size/sample rate. At least 25% of the frequencies
must qualify. Three consecutive valid one-second checks at >=3 dB produce a
visible DRIFT ALERT. Invalid data or a change below threshold resets the alert.
Paused, missing-input and insufficient-reference signal states show Waiting.
RTA shows live programme spectrum; programme spectrum changes alone are not
used as system-drift alerts.

## Local verification

Release app and app tests built using CMake with the installed Apple command-line
tools and macOS SDK 26.5, targeting macOS 26.0 on macOS 26.2 / Apple M4 Max.
The Xcode project build was unavailable because full Xcode is not installed.
Core-only CTest: 122/122 passed. App build CTest: 174/174 passed, including core,
golden, workspace, audio bridge and W7 tests. Public string scan and diff checks
passed.

Reproduce the optional visible synthetic measurement after a Release app build:

```sh
FOH_W7_CPU=1 build-app/app/fohapp_tests 'W7 visible synthetic programme CPU measurement'
```

This feeds synthetic broadband programme through the actual audio callback and
analysis worker at 48 kHz / 512 samples, with a 16384-point FFT and a visible
1000x700 W7 panel. It copies snapshots at the normal model rate, verifies steady
programme, adds +4 dB gain drift, checks the alert, then checks the waiting state
on pause. CPU is process user + system time divided by elapsed time, where 100%
is one logical CPU. The final local measurement was **5.01% over 14.27 s**
on the M4 Max. Warm-up and PNG rendering/export are excluded. A panel PNG
is written to the test executable's JUCE temporary directory for visual review.

M1 CPU acceptance and a physical console/mic show walk-through remain unverified:
this host is an M4 Max. For manual acceptance, repeat the benchmark on M1, then
use console programme to verify the baseline overlay, drift alert, loss-of-signal
waiting state and silent outputs. Do not enable a generator during this check.
