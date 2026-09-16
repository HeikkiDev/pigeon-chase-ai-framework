# ADR-0007 — The shape of the `core/` seams

**Status:** Accepted
**Date:** 2026-09-15
**Affects:** `REQ-DET-001`, `REQ-DET-002`, `REQ-TRK-001`…`REQ-TRK-008`,
`REQ-AIM-001`, `REQ-AIM-002`, `REQ-AIM-003`, `REQ-COM-001`, `REQ-COM-002`,
`REQ-SAF-001`…`REQ-SAF-005`, `REQ-DEV-001`, `REQ-DEV-002`

## Context

ADR-0001 put hardware behind interfaces, ADR-0003 settled track association and
single-target selection, ADR-0004 settled the aiming transform, and ADR-0005
settled where time is allowed to live. What none of them settled is how the
domain is cut into pieces: which component holds state, which components are
pure, what crosses each boundary, and how failure is reported across it.

That decomposition is the hard-to-reverse part. Once tests are written against
these signatures — and in this repository the tests are written first, by a
different agent — changing the shape means rewriting the executable
specification. The constraints that bear on it are:

* `REQ-SAF-002` demands exhaustive proof that no path fires without
  confirmation. Exhaustive enumeration is only feasible over a function whose
  inputs are enumerable.
* `REQ-DEV-002` forbids wall-clock and ordering dependence in tests.
* The deployment target has 1 GB of RAM, so per-frame copies of pixel buffers
  are not acceptable.
* `cpp.instructions.md` forbids exceptions crossing module boundaries.

## Decision

`core/` is cut into five seams, along the boundary between *deciding* and
*doing*.

**1. Detection is an interface, and its result is a closed value type.**
`Detector::detect(const FrameView&) const` returns a `DetectionOutcome` that is
`FOUND` if and only if it carries at least one `Detection`. `FrameView` is a
non-owning `std::span` over pixels the producer owns, so no frame is copied at
the boundary (`REQ-DET-002`).

**2. Tracking is a pure function, with its identity counter threaded through
the caller.** `associate_detections(tracks, outcome, radius, next_id)` returns
the new track set *and* the next unused `TrackId`. No tracker object, no member
counter, no hidden sequence — same arguments, same tracks, always
(`REQ-TRK-007`, `REQ-DEV-002`).

**3. The target state machine is a pure transition function.**
`advance(TargetMachineState, FrameInput) -> TargetTransition`. State is a value
the caller carries; the machine has no clock, no I/O and no members. It emits an
**intent**, never a command: `AIM_AT_TARGET`, `FIRE_AT_TARGET`, `ABANDON`,
`KEEP_SEARCHING`. Confirmation counters live on the tracks, where `REQ-TRK-007`
already put them, so the machine holds only the state and the engaged
`TrackId`.

**4. Authorisation is separate from decision, and is where time lives.**
`SafetyPolicy` takes the `Configuration` and an injected `MonotonicClock` and
turns an intent into a `FireAuthorisation` — granted with clamped angles and a
bounded duration, or refused with a named `FireRefusal` reason. The envelope
clamp (`REQ-AIM-002`), exclusion zone (`REQ-SAF-003`), cool-down and rate limit
(`REQ-SAF-005`) all live here, in the one component that is allowed to know
what time it is (ADR-0005).

**5. The actuator link is a protocol, stated as an interface.**
`ActuatorLink` exposes an aiming command, a fire command and a safe-state
command, each returning a `LinkStatus`. Failure is a value, not an exception,
because `REQ-COM-002` requires the caller to *act* on it. The interface
documents obligations that bind the firmware as much as any C++ class: safe on
construction and destruction, re-check every limit on receipt, self-terminate a
burst.

Two cross-cutting rules follow from this shape:

* **Errors are returned, never thrown.** `std::optional` where absence is the
  whole answer (an empty envelope commands nothing), a named enumeration where
  the reason matters (`LinkStatus`, `FireRefusal`).
* **Physical quantities carry their unit and frame in the type or the field
  name.** `PixelPoint::x_px`, `Angle::degrees`, `MonotonicTimePoint`,
  `std::chrono::milliseconds`. `ServoAngles` documents its datum on every use:
  X = 0° straight ahead, Y = 0° at the horizon.

The consequence worth stating plainly: **the fire path crosses three
independent components.** The state machine says the bird is there, the safety
policy says firing at it is permitted, and the link says the device accepted
it. No single component can fire on its own.

## Alternatives considered

| Option                                                                 | Why it was rejected                                                                                                                                                  |
| ---------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| One `TargetTracker` class owning tracks, state, aiming and firing       | The obvious object-oriented shape, and it destroys the property `REQ-SAF-002` depends on: the fire decision becomes a function of unreachable private state. Exhaustive testing degrades into scenario testing. |
| State machine emits commands directly rather than intents               | Puts clamping, exclusion zones and rate limiting inside the pure function, which then needs the configuration and a clock. ADR-0005 rejected exactly that.               |
| Tracker as a stateful object with an internal identity counter          | Convenient, but the counter is hidden state: two runs of the same scenario can produce different identifiers, and `REQ-DEV-002` becomes unprovable rather than merely untested. |
| Exceptions for link failure                                            | `REQ-COM-002` is ordinary control flow — abandon, do not fire, return to `SEARCHING` — and an exception crossing the device boundary makes the fail-safe path the least-tested one. |
| `std::expected`-style result type for every operation                   | C++23. The project is C++20, and `std::optional` plus a named reason enumeration covers every case here without a vocabulary type of our own.                            |
| A `Frame` value type owning its pixels                                  | A copy per frame at 5 FPS against a 1 GB budget, to solve a lifetime problem that a documented precondition solves.                                                      |
| Deferring the `MonotonicClock` interface until the rate limiter is built | The clock is the one piece that decides whether tests can run without sleeping. Introducing it late means retrofitting every call site that grew a hidden clock in the meantime. |

## Consequences

### Positive

* The safety-critical path is three pure functions and one clock-owning
  policy, all testable by enumeration rather than by scenario.
* `core/` remains free of hardware, I/O, clocks and randomness, which
  `scripts/arch-check.sh` enforces mechanically.
* Every hardware-shaped thing — detector, link, clock — has an interface, so
  the whole pipeline is simulable on macOS (`REQ-DEV-001`).
* Failure at the device boundary is impossible to ignore: every operation
  returns a `[[nodiscard]]` status.

### Negative / accepted trade-offs

* The caller drives the pipeline explicitly rather than calling a single
  `tracker.update(frame)`. The state it carries has since been consolidated
  into one value: ADR-0013 moved the track set and the identity allocator into
  `TargetMachineState`, so the loop carries that alone, and the `REQ-TRK-012`
  retirement cannot be skipped.
* Value semantics mean the track set is copied once per frame. At a handful of
  tracks and 5 FPS this is negligible, but it is a real cost and would not
  survive a hundredfold increase in either number.
* Splitting decision from authorisation means two components must agree about
  what an engagement is. The `TargetTransition` type is the contract between
  them, and it can drift.
* Six open questions (Q10–Q15) were raised while shaping these interfaces. All
  six have since been answered and are recorded in `REQ-DET-004`,
  `REQ-TRK-009`, `REQ-TRK-010`, `REQ-TRK-011`, `REQ-SAF-006` and `REQ-SAF-007`,
  with the non-obvious ones carried by ADR-0009 through ADR-0012. One new
  question (Q16) emerged from combining two of those answers; it has been
  answered too, and is carried by `REQ-TRK-012` and ADR-0013 — the only answer
  that required a signature change here.

### Follow-up work

* Specify the wire protocol behind `ActuatorLink` in `docs/architecture/`
  (`REQ-COM-001`); the interface is the operations, not the encoding.
* Design the application loop that owns the track set and drives these seams.
* Simulated implementations of `Detector`, `ActuatorLink` and `MonotonicClock`
  (ADR-0001), which are production code, not test doubles.

## Verification

* `scripts/arch-check.sh` fails the build on any hardware header, I/O header,
  device path, wall-clock read or unseeded generator under `core/`. The
  pure-function claim is mechanically enforced for the parts a grep can see.
* `core/src/public_headers.cpp` includes every public header, so the interfaces
  are compiled under `-Werror` and analysed by clang-tidy from the moment they
  exist. A header nothing includes is a header nothing checks.
* The absence of a clock in the state machine is visible in its signature:
  `advance` takes no clock and `core/` has no way to obtain one except through
  an injected `MonotonicClock`.
* `scripts/trace.sh` reports every requirement above as UNVERIFIED until the
  test-engineer writes the failing suite against these signatures. That report
  is the outstanding-work list for this decision.
