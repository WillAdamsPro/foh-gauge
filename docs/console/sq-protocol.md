# SQ console control: wire-capture notes

## 1. Provenance and status

This implementation uses the task's capture-derived facts from Will's own wire
captures. They were cross-checked against hrueger/allen-heath-sq-tools (MIT):
https://github.com/hrueger/allen-heath-sq-tools

The requested source `/workspace/sq-capture-analysis.md` was not present on this
checkout's machine. This document records the supplied task facts and explicitly
marks provisional details; it is not a copy of that missing analysis. Replace or
reconcile it with a sanitized copy when available. No proprietary binary or
application code is used. Host addresses, hostnames and strip names are excluded.

The checked-in fixture is a flattened list of 87 raw eight-byte frames from
captures 1 and 2. Its group labels were removed. It does not contain a firmware
version reply or evidence that validates the handshake on firmware 1.6.3.

## 2. Frames

TCP uses port 51326. Register frames contain eight bytes:

`f7 prefixCategory CAT REG B3 MOD lo hi`

The value is unsigned 16-bit little endian. The first category byte can differ
from CAT; preserve it during decoding/re-encoding. Cache keys use CAT, REG, B3,
and MOD. Incoming standalone register frames provide current-value evidence.

Envelope frames are `7f sub len32 payload`, with a six-byte header and a
little-endian 32-bit payload length. TCP may fragment or coalesce either format.
Opaque dump envelopes are not scanned for register markers or interpreted as
read-back until their layout is confirmed. Receive memory is bounded.

## 3. Confirmed parameter encodings

| Parameter | Encoding | Request clamp |
| --- | --- | --- |
| PEQ gain | round(32768 + dB × 256) | −15 to +15 dB |
| PEQ frequency | round(−9206 + 15308 × log10(Hz)) | 20 to 20,000 Hz |
| Output delay | round(ms × 96) | 0 to 682 ms |
| Input delay (read-only) | round(ms × 96) | 0 to 341 ms |
| Output delay in | 0 out, 1 in | Boolean |

## 4. Address map (pending firmware profile 1.6.3)

Input n uses B3 = n − 1 (1–48). Aux 1 uses B3 = 0x73. Matrix 1 uses
B3 = 0x58, one address for the stereo strip. Other Aux/Matrix addresses are
unverified and refused; no arithmetic extrapolation is permitted.

PEQ uses category 0x11, modifier 0:

| Band | Gain | Frequency | Q (unverified) | Shape (unverified) |
| --- | --- | --- | --- | --- |
| LF | 0x0c | 0x0d | 0x0e | 0x0f |
| LM | 0x10 | 0x11 | 0x12 | — |
| HM | 0x13 | 0x14 | 0x15 | — |
| HF | 0x16 | 0x17 | 0x18 | 0x19 |

PEQ in is category/register 0x11/0x1a, but remains read-only pending capture 3.
Delay uses category 0x14, modifier 0: 0x0c is in, 0x0d is time.
Only output delay time/in and PEQ gain/frequency are candidates for writes.
No fader, mute, routing, scene, GEQ or dynamics frame can enter the write queue.

## 5. Provisional read-only connection sequence

The task specifies: exchange 0x00 UDP port, send 0x01, receive 0x02 version,
then 0x14, 0x0b, 0x0a subscribe, followed by 0x0d/0x11/0x0f.
The read-only state machine uses the following provisional payload expectations,
which must be checked against the missing analysis and capture 3:

1. Bind a local UDP port; send 0x00 with the two-byte port in little endian.
2. Wait for a two-byte 0x00 reply; send an empty 0x01 envelope.
3. Wait for a 12-byte 0x02 version reply; retain all bytes without guessing
   which fields represent firmware. Send empty 0x14.
4. Wait for nonempty 0x15 init-state envelope; send 0x0b with `02 00`.
5. Wait for 0x0c with `01 00`; send 0x0a with 8192 bytes of `ff`.
6. Send empty 0x0d, 0x11 and 0x0f envelopes, paced 40 ms apart.

UDP keepalive is `7f 05 00 00 00 00` to port 51324 every second.
All I/O runs on a background thread, with bounded connection/handshake waits.
Unknown firmware can subscribe/read but cannot send parameter writes.
The firmware allow-list is intentionally empty until exact capture bytes are
recorded. A human-readable “1.6.3” label does not authorize writes.

## 6. Capture 3 blockers

- Obtain and sanitize the original capture analysis document.
- Confirm handshake envelope lengths/payloads on 1.6.3 and record the exact
  0x02 version payload before populating the firmware allow-list.
- Confirm init-dump offsets before using any dump to establish read-back.
- Confirm Q scale (`round(Q × 8)`), shape values (0 bell, 6 shelf, 11 pass),
  and PEQ in. `kExperimentalParams` is false and these writes are disabled.
- Confirm other Aux and Matrix addresses individually before adding them.
- Live testing requires Will's approval and an attended session. All tests in
  this change are offline; no console discovery or live connection is performed.

## 7. Write and undo contract

Every parameter requires a current standalone echo from this connection. Clamp
requests, compare wire values, show the change list, and require confirmation.
First-use dry-run is enabled and sends no parameter bytes. Unknown firmware
refuses both live writes and application of a dry-run batch through the write
gate; review remains available when echoes exist.

Accepted live batches snapshot exact prior frames, including differing category
prefixes, into an undo history in session measurement data. Undo reviews these exact bytes, requires
fresh read-back and confirmation, and uses the same firmware/address gates and
25-frame-per-second queue. A session restores only undo data, never connection,
firmware authorization, cache evidence or runtime enable state. Further batches
wait for desk-side echoes of the preceding batch. Enqueueing never updates cache.
Disconnects clear evidence and pending sends. A socket failure can partially
apply a batch; the entire prior snapshot remains available for review after a
fresh connection/read-back. Reconnection and session load require reviewing
that the connected desk is the intended desk before restoring prior values.
