# ADR-0005 — Time is counted in frames, except where it cannot be

**Status:** Accepted
**Date:** 2026-09-15
**Affects:** `REQ-TRK-004`, `REQ-SAF-001`, `REQ-SAF-005`, `REQ-DEV-002`
**Resolves:** Open question Q2

## Context

Q2 asked for the target frame rate and whether the `REQ-TRK-004` verification
frame has a timeout.

The timeout half of that question is not a tuning detail. It decides whether
the target state machine — the safety-critical heart of the system — needs to
read a clock. A clock in the state machine means a clock interface injected
through `core/`, a fake clock in every test fixture, and a class of
timing-dependent tests that `REQ-DEV-002` exists to forbid.

But two other requirements genuinely are about elapsed real time: the bounded
fire duration (`REQ-SAF-001`) and the cool-down and engagement rate limit
(`REQ-SAF-005`). Water flows in seconds, not in frames, and it keeps flowing
whether or not another frame ever arrives.

So the question is not "clock or no clock". It is *where* the clock is allowed
to be.

## Decision

**The target state machine counts frames, not milliseconds.** Verification uses
the immediately following frame, with no wall-clock deadline. If no further
frame arrives, the engagement simply never resolves and no fire command is
issued — the failure mode is inaction, which is the safe one.

**Rate limiting uses an injected monotonic clock.** Cool-down and engagements
per minute are enforced by a policy component that takes a `MonotonicClock`
interface, with a simulated clock in tests. This component sits outside the
state transition function.

**Fire duration is enforced in firmware.** The Arduino bounds the burst itself,
using its own timer, so the limit holds even when the Raspberry Pi stops
talking mid-engagement.

Target throughput is 5 FPS on the Pi 3B (`REQ-DET-003`). Confirmation takes
three frames and verification a fourth, so an engagement resolves in roughly
800 ms — fast enough that the bird is usually still where it was seen.

## Alternatives considered

| Option                                                       | Why it was rejected                                                                                                    |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------ |
| Wall-clock timeout on the verification frame                  | Pushes a clock into the safety-critical state machine and makes its tests timing-dependent, in direct conflict with `REQ-DEV-002`. |
| A timeout expressed as "N frames may elapse"                  | Defensible, and it stays clock-free. Rejected as unnecessary: `REQ-TRK-004` already says the *next* frame, and N is another parameter to justify, calibrate and test. Revisit if dropped frames turn out to be common. |
| No rate limiting at all                                       | `REQ-SAF-005` requires it, and an unlimited system harasses one bird indefinitely and abuses the pump duty cycle.          |
| Enforce fire duration only on the Raspberry Pi                | The limit would fail in exactly the scenario it exists for: the Pi hanging while the valve is open.                        |
| Let `core/` read `std::chrono::steady_clock::now()` directly  | Forbidden by `core.instructions.md`, unenforceable in tests, and now mechanically rejected by `scripts/arch-check.sh`.     |

## Consequences

### Positive

* The state machine stays a pure function of (state, tracks, detection), so
  exhaustive path testing for `REQ-SAF-002` remains feasible.
* No `sleep`, no timing tolerance and no flakiness in the default suite.
* The fire-duration bound survives a Raspberry Pi failure.
* The clock exists in exactly one place, behind one interface, easy to audit.

### Negative / accepted trade-offs

* Frame-counted verification is only as timely as the frame rate. If detection
  stalls, a locked target stays locked indefinitely. Inaction is safe but it is
  not free: the rig may sit pointed at a departed bird.
* Two notions of time now coexist — frames in the state machine, seconds in the
  rate limiter. That boundary must stay obvious, or someone will eventually
  compare one to the other.
* The 5 FPS figure is an estimate for an unchosen detection model. It is a
  target to verify at bring-up, not a measured result, and it is verified by a
  hardware-in-the-loop test outside CI (`REQ-DEV-003`).

## Verification

* `REQ-TRK-004` acceptance asserts that no clock is read on the verification
  path, and that an absent frame yields no fire command.
* `REQ-SAF-005` acceptance asserts rate limiting is driven by an injected
  monotonic clock.
* `REQ-SAF-001` acceptance asserts the actuator deactivates after a simulated
  link goes silent.
* `scripts/arch-check.sh` mechanically rejects any wall-clock read that appears
  in `core/`.
