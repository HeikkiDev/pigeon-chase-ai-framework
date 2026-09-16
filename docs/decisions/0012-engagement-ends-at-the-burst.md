# ADR-0012 — An engagement ends at the burst, and re-engagement is bounded by the rate limiter

**Status:** Accepted
**Date:** 2026-09-16
**Affects:** `REQ-TRK-001`, `REQ-TRK-011`, `REQ-SAF-005`,
`core/include/pigeon/core/target_state.hpp`

## Context

`REQ-TRK-005` describes how an engagement ends when the target is **lost**.
Nothing described how it ends when it **succeeds**: after
`EngagementIntent::FIRE_AT_TARGET`, which state does the machine enter, and may
the bird just sprayed be engaged again?

Two constraints bound the answer. `REQ-TRK-001` fixes the machine at exactly
three states, so a `COOLING_DOWN` or `ENGAGEMENT_COMPLETE` state is not
available. And `REQ-TRK-006` makes `TARGET_LOST` reachable only from
`TARGET_LOCKED` on a loss, so a successful burst cannot route through it.

The remaining question is whether the system should remember which tracks it
has already fired upon and decline to fire on them again.

## Decision

After the fire command is issued the machine returns to `SEARCHING` with no
engaged track. The same track **may** be engaged again, and the only thing
standing between two bursts at the same bird is the `REQ-SAF-005` cool-down and
engagement rate limit, enforced by `SafetyPolicy`.

The state machine keeps no memory of what it has fired upon. Repetition is
bounded by the rate limiter alone. This is recorded normatively as
`REQ-TRK-011`.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Add a fourth state for the post-fire period | Breaches `REQ-TRK-001`'s "exactly three states". It would also put elapsed time into the state machine, which ADR-0005 deliberately kept out of it. |
| Remember fired-upon tracks and never re-engage them | Requires unbounded memory of track identifiers on a device that runs for months, and is wrong on the merits: the bird just sprayed is the one most likely still sitting there. Deterrence that gives up after one attempt is not deterrence. |
| Route the successful path through `TARGET_LOST` | Contradicts `REQ-TRK-006`, and would make the state trace lie about what happened: the target was hit, not lost. |
| Hold `TARGET_LOCKED` until the cool-down expires | Needs a clock inside the pure function (ADR-0005) and would misreport the rig as engaged while it is idle. |

## Consequences

### Positive

* Exactly three states, as `REQ-TRK-001` requires, with the success path and
  the loss path both terminating in `SEARCHING`.
* Repetition is governed in one place — the rate limiter — where it is
  configured, auditable and testable by advancing a simulated clock.
* The machine carries no history, so its state space stays small enough to test
  exhaustively, which is what makes `REQ-SAF-002` checkable.

### Negative / accepted trade-offs

* A persistent bird will be sprayed repeatedly, at the configured rate: at most
  six bursts per minute (`REQ-SAF-005`). That is the intended behaviour, and
  the ceiling is the thing to tune if it proves excessive.
* Combined with immediate track discard (ADR-0010), this decision initially
  left a continuously detected bird carrying its confirmation across the burst,
  so it was immediately re-confirmable and the rig re-aimed at once. That gap
  has since been closed: `REQ-TRK-012` resets the count when an engagement
  ends, and ADR-0013 retires the engaged track to realise it. Re-engagement is
  therefore bounded by three fresh detections *and* the cool-down.

### Follow-up work

* Resolved. Open question Q16 — whether the engaged track's
  consecutive-detection count is reset when an engagement ends — was answered
  by the maintainer in the affirmative, for both the burst and the abandon
  path. It is recorded in `REQ-TRK-012` and ADR-0013, which also decides where
  the reset is applied so that a caller cannot omit it.

## Verification

* `REQ-TRK-011` is a declared requirement, so `scripts/trace.sh` reports it
  `UNVERIFIED` until a test cites it.
* Directly testable as a pure transition: from `TARGET_LOCKED` with the engaged
  track present, assert the transition carries `FIRE_AT_TARGET` and a next
  state of `SEARCHING` with no engaged track.
* The bound on repetition is verified in `SafetyPolicy`'s tests, not the state
  machine's: with a simulated clock, a second authorisation within the
  cool-down must be refused `COOLING_DOWN`, and a seventh within a minute must
  be refused `RATE_LIMIT_REACHED`.
* The re-confirmation half of the bound is verified against `REQ-TRK-012`; see
  ADR-0013.
