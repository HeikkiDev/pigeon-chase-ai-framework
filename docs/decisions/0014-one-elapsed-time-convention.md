# ADR-0014 — One elapsed-time convention for every time limit

**Status:** Accepted
**Date:** 2026-02-19
**Affects:** REQ-SAF-005, REQ-SAF-007, `core/include/pigeon/core/safety_policy.hpp`, `core/include/pigeon/core/clock.hpp`

## Context

`REQ-SAF-005` imposes two time limits on firing: a cool-down of two seconds
between bursts, and a maximum of six bursts in any one minute. Both are
evaluated against the injected monotonic clock of ADR-0005; neither may read
the wall clock.

Writing the acceptance suite exposed that neither limit said what happens *at*
the boundary. Is a burst permitted exactly two seconds after the previous one,
or must it wait a further tick? Does a burst that is exactly one minute old
still occupy a slot in the rate window? The test-engineer could not assert
either edge and left both unasserted, with a comment, rather than guess.

The gap is small in observable terms — a monotonic clock has finite resolution,
and exact coincidence is rare — but it is not small in specification terms. An
unstated boundary is filled in independently by the implementer and by the
test author, and the two guesses need not agree. Worse, the cool-down and the
rate window could each acquire their own accidental convention, so that
reasoning about one taught you nothing about the other.

## Decision

One convention governs every duration and every window in `core/`:

* **A period has elapsed when `elapsed >= duration`.** A period of duration *D*
  beginning at *t* occupies the half-open interval `[t, t + D)`. The instant
  *t + D* is outside it, and is therefore the first instant at which the period
  counts as over. A burst exactly `cool_down` after the previous transmitted
  burst is **permitted**.

* **A window of length *D* ending at `now` is half-open the same way.** It
  contains every recorded instant strictly later than `now - D`. A burst
  exactly one minute old has **left** the window and no longer occupies a slot.

The two statements are the same statement seen from each end, which is the
point: there is one rule to learn, one rule to test, and one rule to get wrong.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Closed intervals: a period elapses only once `elapsed > duration` | Makes a two-second cool-down last strictly longer than two seconds, so the configured number is not the observed number. Requires the implementer to write `>`, which is not what anyone writes by default. |
| Leave it unspecified, on the grounds that exact coincidence is improbable | This is precisely the hole that produced the question. "Improbable" is not "impossible", and a simulated clock in a test advances by exact integers, so the boundary is reached on purpose in every test that tries to pin it down. |
| One convention for the cool-down, another for the rate window, each chosen for local convenience | Two rules drift. A reader who has learned one has learned nothing about the other, and a reviewer must check both every time either changes. |
| Express the limits as instants rather than durations, e.g. `next_permitted_at` | Pushes the same boundary question one level down without answering it, and adds an interface concept for no gain. |

## Consequences

### Positive

* Each limit has a boundary value a test can assert *at* the edge, and
  `REQ-SAF-005` now carries exactly such acceptance criteria.
* The convention is what an implementer writing the obvious `>=` comparison
  gets by default, so honouring it requires no special care and violating it
  requires deliberate effort.
* The configured duration is the observed duration: a two-second cool-down
  blocks for two seconds, not two seconds and a tick.

### Negative / accepted trade-offs

* The convention is permissive at the boundary in both directions — it fires at
  the earliest defensible instant and retires a rate-window entry at the
  earliest defensible instant. Where the two compound, the rig is marginally
  more willing to fire than a closed-interval reading would be. The margin is
  one clock tick, which is far below the resolution at which the deterrent
  behaviour is specified, and the alternative buys nothing for the cost of a
  counter-intuitive rule.
* It must be applied to every future time limit, not just these two, or the
  drift this ADR exists to prevent starts again.

### Follow-up work

* None. Both current limits are covered.

## Verification

* `REQ-SAF-005` carries boundary acceptance criteria that name the exact
  instant: a burst at exactly the cool-down duration is permitted, and a burst
  exactly one minute old no longer counts towards the rate limit. These are
  assertable against the injected `MonotonicClock` by advancing a simulated
  clock to the precise value, with no waiting and no wall clock.
* `scripts/trace.sh` requires `REQ-SAF-005` to name the tests that verify it,
  so the boundary criteria cannot quietly go uncovered.
* `scripts/arch-check.sh` continues to forbid reading a real clock in `core/`,
  which is what makes the boundary reachable in a test at all (ADR-0005).
