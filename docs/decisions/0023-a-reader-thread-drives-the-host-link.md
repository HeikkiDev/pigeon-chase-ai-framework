# ADR-0023 — A reader thread drives the host link, and health is read without a lock

**Status:** Accepted (maintainer, 2026-11-26)
**Date:** 2026-11-26
**Affects:** `REQ-COM-003`, `REQ-COM-002`, `REQ-DET-003`, `REQ-DEV-002`;
`raspberry/link/`; ADR-0021 (follow-up 3), ADR-0016

## Context

`SerialActuatorLink` (ADR-0021, follow-up 2) is poll-driven and not thread-safe.
Its owner must call `poll()` at least every reader period, **100 ms**, between
commands, or the stall rule of `device-protocol.md` section 7 discards what the
line buffered as a backlog after **200 ms**.

The caller that sends commands is the engagement loop, which runs once per camera
frame. `REQ-DET-003` asks for inference at **5 frames per second**: about 200 ms
spent inside the model per frame, during which a single-threaded loop can poll
nothing. So one loop cannot both run the detector and keep the link read. Something
else has to read the line while inference runs.

That something runs alongside commands, which `SerialActuatorLink` cannot take
concurrently, so access has to be serialised. But the most important query does not
tolerate waiting: `health()` must transmit nothing, **block on nothing** and change
nothing (`REQ-COM-003`, ADR-0016). An exchange holds the line for up to the write
timeout plus the reply timeout, about 200 ms. A `health()` that took the same lock
would wait that long, which is the blocking ADR-0016 rules out.

## Decision

**D1 — One wrapper owns the link and serialises it.** A new class in
`raspberry/link/` implements `ActuatorLink` and owns a `SerialActuatorLink`. One
mutex serialises everything that touches it: polling, the three commands and
`start_up`. Nothing else holds a reference to the inner link.

**D2 — `health()` reads a published snapshot, never the lock.** After every poll
and every command, while still holding the mutex, the wrapper publishes the inner
link's health state: whether the line is open, whether and when the device was last
heard, and the `faulted` and `rejected` flags. `health()` reads that snapshot and the
injected clock and applies the rule of section 7, so silence is still judged against
the time *now*, not the time of the last poll. The snapshot is packed into one
`std::atomic<std::uint64_t>`, asserted at compile time to be always lock-free, so a
reading is never torn and never waits. The rule itself is one function shared with
`SerialActuatorLink::health()`, so the two cannot disagree.

**D3 — The thread is separate, small and stoppable.** A periodic runner owns one
`std::thread` that calls the wrapper's single-poll operation every reader period,
waiting on a condition variable so that stopping it is immediate. The wrapper stops
and joins it **before** the inner link is destroyed, so the shutdown `SAFE_STATE`
(section 8) is sent with no reader running beside it.

**D4 — Logic is tested without threads; the thread is tested without timing.** The
snapshot packing, the shared health rule and the wrapper's single-poll operation are
tested deterministically against the simulated device, with no thread. The runner is
tested only for what holds whatever the machine's load: it calls its function, and
stopping it joins. Bounds exist only to stop a failing test from hanging
(`REQ-DEV-002`). That `health()` takes no lock is tested by calling it from inside an
exchange that runs on a helper thread, while the wrapper holds its mutex: a
`health()` that locked would deadlock, and a bounded wait on the main thread turns
that into a failure, ending the process because a deadlock cannot be undone.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| One single-threaded loop that also polls | Inference takes about 200 ms per frame at 5 FPS; nothing can poll during it, so the stall rule would discard replies and heartbeats. |
| Inference on a worker thread, link on the main loop | Keeps the link untouched but changes how the detector and core's per-frame loop are driven; a larger change for the same result. |
| Relax the protocol timings above the inference time | Changes ADR-0021's parameters and the specification, and lengthens how long health can read `OK` after the device stops (now about 1.7 s). |
| `health()` takes the same mutex | Waits up to about 200 ms during an exchange: the blocking `REQ-COM-003` and ADR-0016 forbid. |
| A recursive or shared (reader-writer) mutex | A reader still waits for a writer mid-exchange; only the waiting moves. |
| `std::atomic` of a struct | Not guaranteed lock-free; it may use a hidden lock. One `uint64_t` is lock-free on arm64 and x86-64, and the code asserts it. |
| A seqlock | Correct but subtle, for four small fields that fit in one word. |
| Make `SerialActuatorLink` itself thread-safe | Puts locking into the protocol logic that is tested single-threaded and deterministically; the wrapper keeps the two concerns apart. |

## Consequences

### Positive

* The line is read every 100 ms whatever the detector is doing, so the stall rule
  holds as specified and no reply is lost to inference.
* `health()` never waits, so `REQ-COM-003` holds with a reader running.
* `SerialActuatorLink` is unchanged apart from exposing its health state, and stays
  single-threaded and deterministically tested.

### Negative / accepted trade-offs

* A thread in `raspberry/`. `core/` and `protocol/` stay single-threaded and
  clock-free.
* During an exchange `health()` reports the state from before it, at most one
  exchange old (about 200 ms). It is the same thread that sends and then asks, so in
  the engagement loop it sees the outcome; a second thread asking mid-exchange sees
  the previous state, which is what "answered without an exchange" means.
* The reader period is real time on the device. It is a parameter (section 9), and
  the first hardware run may move it.
* The test that `health()` takes no lock relies on a deadlock being caught by a
  bounded wait and ending the test process, which is a blunt instrument; it is the
  only way to observe a lock that should not be there. Mutating `health()` to take
  the lock makes it fail in 5 s.

### Follow-up work

* Wiring: whatever starts the application on the Pi constructs the port, the clock
  and this wrapper, calls `start_up`, then hands it to `core/` as its
  `ActuatorLink`. That is the application's entry point and is not built yet.
* The hand-run check against a real Arduino (ADR-0021, follow-up 3).

## Verification

* Deterministic tests of the snapshot packing and the shared health rule, including
  every combination of open, heard, silent, faulted and rejected.
* Wrapper tests against the simulated device with no thread
  (`tests/raspberry/link/concurrent_actuator_link_test.cpp`): a poll publishes what
  it heard; a command publishes its outcome; silence is judged at the time of the
  query; `health()` from inside an exchange returns without the lock.
* Runner tests (`periodic_runner_test.cpp`): it calls its work, stopping joins it
  and is immediate mid-period, with bounds only to stop a failing test from hanging.
* End to end through a pseudo-terminal with the real clock and the reader thread
  (`concurrent_actuator_link_reader_test.cpp`): a heartbeat makes the link healthy
  with no caller polling, and shutdown stops the reader and sends `SAFE_STATE`.
* The suite runs under AddressSanitizer and UBSan in CI. ThreadSanitizer is not in
  CI; the threaded tests were run under it by hand when this was built, and adding
  it to CI is follow-up if the threading grows.
