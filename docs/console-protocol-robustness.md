# Console protocol hardening blocker

Reviewed base: `origin/master` at `874bd72911b3c2a8ab67bec4dd5c02aef8033e4b`.

`CODEX_TASKS.md` task 45 specifies console control, but this base has no
`app/Source/console/` directory, X32 OSC/message parser, console transport,
write-confirmation state, protocol fixtures, or SQ blocker notes. Neither the
app nor test CMake targets include a console module. Existing console references
are measurement guidance and delay formatting, not network control.

The requested hardening is deferred until the console implementation lands.
Implementing task 45 from scratch would be a new feature, beyond this pass.
No protocol fixes or protocol tests were added, and no networking or audio
callback code was changed. Passing the existing suites does not establish
console protocol safety or confirm-before-write behavior.

Once the implementation is available, the focused regression pass must cover:

- Synthetic OSC/message recordings with malformed, truncated, oversized and
  out-of-range payloads; deterministic mutations under ASan/UBSan.
- Timeout, disconnect and reconnect during reads and writes, including partial
  operations and stale queued work.
- Explicit confirmation for every write, with pending confirmation invalidated
  across reconnect and session load; no automatic write retries using old consent.
- Network work isolated from audio callbacks, with no callback allocations,
  locks or I/O.
- SQ blockers documented against the available implementation, without guessing
  protocol details or adding unsupported control features.

Local baseline validation: Apple Silicon, macOS 26.2, AppleClang Command Line
Tools, deployment target 26.0. Xcode is absent; the app uses Unix Makefiles.

- Core Release: build succeeded; 142/142 CTest tests passed.
- App Release (`FOHGauge` and `fohapp_tests`): build succeeded;
  220/220 CTest tests passed, including the existing audio callback allocation
  regression. Existing compiler warnings remain outside this scope.
