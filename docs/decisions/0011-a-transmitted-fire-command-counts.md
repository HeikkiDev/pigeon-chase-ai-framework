# ADR-0011 — A transmitted fire command counts, whatever the reply

**Status:** Accepted
**Date:** 2026-09-16
**Affects:** `REQ-SAF-005`, `REQ-SAF-007`, `REQ-COM-002`,
`core/include/pigeon/core/safety_policy.hpp`

## Context

`REQ-SAF-005` bounds firing by a cool-down and an engagements-per-minute
ceiling. It says "after firing" — which is unambiguous when the Arduino
acknowledges the command, and silent when it does not.

The serial link can reject a command, return a transport failure, or simply
not answer. In every one of those cases the host does **not** know whether
water left the nozzle. The command may have arrived and been executed with the
acknowledgement lost on the way back; it may never have arrived at all.
`REQ-COM-002` tells the host what to do about the engagement — abandon it —
but not what to tell the rate limiter.

The failure modes are asymmetric, and that asymmetry decides this:

* Counting a command that did not fire costs **one missed deterrent**, delayed
  by the cool-down.
* Not counting a command that did fire permits an immediate retry. On a flaky
  link — a loose connector, a browning-out regulator — that is a loop that
  fires repeatedly at the same bird with no cool-down between bursts, which is
  exactly the harassment `REQ-SAF-005` exists to prevent.

## Decision

Any fire command that was **transmitted** to the actuator link counts towards
the cool-down and the engagement rate limit, regardless of the `LinkStatus`
returned. Only a command that was never sent — one refused by
`SafetyPolicy::authorise_fire` — does not count.

The interface names this: `SafetyPolicy::record_fire_sent()`, called once per
command handed to `ActuatorLink::send_fire_command`, before the reply is
inspected. This is recorded normatively as `REQ-SAF-007`.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Count only commands the Arduino acknowledges | Makes the rate limit depend on the reliability of the reply path. A link that delivers commands but loses acknowledgements would fire without limit — the one failure mode where a limiter is most needed. |
| Count `REJECTED` but not `TRANSPORT_FAILURE` | Assumes a transport failure means the command did not arrive. A write that fails after the bytes have gone out, or a timeout on the reply, says nothing of the kind. The distinction is not knowable from the host. |
| Ask the Arduino how many bursts it has fired and reconcile | Adds a protocol message, a new failure mode of its own, and host state that can disagree with firmware state. `REQ-COM-001`'s protocol is deliberately minimal. |

## Consequences

### Positive

* The rate limiter degrades safely: a broken link produces fewer bursts, never
  more.
* The bookkeeping depends only on what the host did, not on what it was told,
  so it is deterministic and testable with a simulated link that returns any
  `LinkStatus` (`REQ-DEV-002`).
* No new protocol message, so `REQ-COM-001` is untouched.

### Negative / accepted trade-offs

* A bird may escape unsprayed when a command is lost, and the rig will wait out
  the full cool-down before trying again. Accepted deliberately: this system's
  worst outcome is spraying too much, not too little.
* The host's burst count may exceed the Arduino's actual count. Nothing depends
  on the two agreeing; the firmware enforces its own duration bound
  independently (`REQ-SAF-001`).

### Follow-up work

* The simulated `ActuatorLink` in the test suite must be able to return each
  `LinkStatus`, so the "sent but rejected still counts" case is exercised.

## Verification

* `REQ-SAF-007` is a declared requirement, so `scripts/trace.sh` reports it
  `UNVERIFIED` until a test cites it.
* Directly testable without hardware: drive a `SafetyPolicy` with a simulated
  monotonic clock and a simulated link that returns `REJECTED`, record the
  send, advance the clock by less than the cool-down, and assert the next
  authorisation is refused with `FireRefusal::COOLING_DOWN`.
* `scripts/arch-check.sh` guarantees the clock is injected rather than read
  from the wall clock, so the cool-down can be tested by advancing time rather
  than by waiting (ADR-0005).
