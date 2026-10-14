# Console control — task 45 status

Task 45 is **not complete**: X32 support is implemented; SQ-5 firmware
1.6.3 EQ/delay access remains blocked on verified protocol evidence.

Open File > Console Control. Enter a numeric IPv4 address, select a mix bus
or matrix, then Connect / Read. Choose an EQ band and parameter to see its
read-back. Enter a proposed value and Review change. The confirmation names
the console address, bus/matrix, band or output slot, parameter, and value.
Cancel, stale confirmations, changed targets/addresses, invalid parameters,
and replayed confirmations do not enqueue writes. Each accepted confirmation
is consumed before transport invocation. Read again to verify a queued write;
UDP transport does not acknowledge delivery.

X32 has six PEQ bands on buses 1–16 and matrices 1–6. Frequency is 20–20000 Hz,
gain ±15 dB, and Q 0.3–10. Enable values are 0/1. Filter type is the console's
integer enum (0–5 for buses; 0–8 for matrices). X32 delay belongs to output
slots 1–16, not bus/matrix DSP. Select an output explicitly. Its `/src` reply
must match the chosen bus/matrix before proposing delay (0.3–500 ms) or delay
enable. An observed routing change invalidates pending confirmation. The
routing read-back is a snapshot: read again after changing console routing.
No routing writes, generator writes, or automatic application are included.

`fohconsole` is an app-owned pure C++ library with no DSP/JUCE dependency.
The UI/control state has one message-thread owner. Only the console worker
owns socket I/O; its bounded queues and locks are unrelated to audio paths.
It has no references to AudioEngine or AnalysisEngine. Destruction stops and
joins the worker. No console operation runs in an audio callback.

## Protocol evidence and remaining SQ work

- [X32 protocol research and implementation](https://github.com/pmaillot/X32-Behringer):
  `X32Bus.h`, `X32Mtx.h`, `X32Output.h`, `SetSceneParse.c` establish OSC
  addresses, argument types, EQ normalisation, output-source IDs and delay
  normalisation. UDP port 10023, address-only reads, big-endian int32/float32.
- [Official SQ firmware 1.6 MIDI protocol](https://support.allen-heath.com/hc/en-gb/articles/47793519540497-SQ-MIDI-Protocol-Firmware-V1-6):
  MIDI/TCP port 51325 exposes no EQ/delay parameter addresses.
- [Independent SQ MixPad protocol research](https://github.com/hrueger/allen-heath-sq-tools):
  `packages/sq-api/src/transport/frame.ts`, `api/channel.ts`, and
  `api/conversions.ts` provide TCP 51326 framing and provisional mix-bus
  EQ/delay encodings. Its source and feature documentation disagree on delay
  scaling and Q ranges, and do not establish matrix addresses or firmware
  1.6.3 compatibility. Those encodings are isolated for fixture testing only;
  SQ connection and all SQ writes are disabled. No matrix addresses are
  inferred from a show-file layout.

To finish SQ: obtain a firmware 1.6.3 handshake and synthetic mix-bus/matrix
read/write recordings covering EQ enable, frequency, gain, Q, shape, delay
enable and delay duration; establish channel mapping and units; implement
TCP handshake/subscription and initial-state read-back; then enable SQ only
after the same confirmation and transport tests pass. No hardware recordings,
console IP addresses, or production data are stored here.

`tests/test_console.cpp` contains literal synthetic wire examples, not real
hardware captures. Tests check bytes independently of encoder/decoder round
trips, X32 target boundaries/transforms, malformed messages, fragmented SQ
framing, routing mismatch, cancellation, proposal replacement, confirmation
replay and failed transport. `fohconsole_tests` also runs in core-only builds.

## Local validation

- Release core-only build and CTest: 131/131 passing.
- Release macOS app and tests: arm64, minimum macOS 26.0; 178/178 CTest cases passing.
- ASan/UBSan console protocol/controller tests: 9 cases, 3534 assertions passing.
- Public-string scan and tracked calibration-file guard pass.
- Xcode is not installed on this Mac; the app was built with the existing
  CMake Makefiles generator and Apple command-line tools.
- No console hardware was contacted. SQ acceptance criteria remain unmet.
