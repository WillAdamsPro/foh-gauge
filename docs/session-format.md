# Session files

File > Save Session, Open Session and Save Session As use portable `.fohsession`
folders. Shortcuts are Cmd+S, Cmd+O and Shift+Cmd+S. Select the session folder
when opening, rather than the manifest inside it.

Version 1 contains:

- `session.json`: version, audio device type/names, sample rate, buffer size,
  active channel masks, mic/reference/output routing, calibration enabled/name,
  capture count, current workspace (including floating windows and panel state),
  measurement settings and reference delay.
- `mic-cal.txt`: a copy of the loaded calibration text, when present. The original
  file is no longer needed after saving.
- `captures/0.json` through `captures/7.json`: stored trace name, visibility,
  reference delay and complete TF arrays/settings. Undefined real-valued bins
  are JSON null and load as NaN.
- `targets/targets.json`: optional opaque target JSON. `hasTargets` in the manifest
  indicates its presence.
- `splCal` in the manifest: optional opaque SPL calibration JSON.

SPL calibration and target types are not yet implemented in the app. Opening and
resaving a session preserves these optional fields for future wiring. Missing
optional fields mean uncalibrated/no targets.

Opening restores the current workspace and session-wide routing. Saved devices
are checked against the available device type/input/output names. A missing
device or unavailable device configuration opens Setup and displays a notice.
The generator is off after opening. Session I/O runs on the message thread;
the audio callback remains allocation-, lock- and I/O-free.

Saves write a sibling `.saving` folder, move the previous bundle to `.backup`,
then rename the completed folder into place. Failures roll back the previous
bundle. An existing recovery folder blocks another save so it is not overwritten.
Unsupported versions, missing assets, symbolic-link assets, malformed JSON and
inconsistent capture arrays fail before session data is applied. Asset paths are
fixed within the bundle rather than read from user-provided manifest paths.
