# ADR-0018 — A refused burst still ends the engagement, and `TARGET_LOST` does not persist

**Status:** Accepted
**Date:** 2026-09-20
**Affects:** REQ-TRK-005, REQ-TRK-012, ADR-0011, ADR-0013, `core/include/pigeon/core/target_state.hpp`,
`core/src/target_state.cpp`

## Context

Two behaviours were implemented as *derived* readings — consistent with the
headers and the acceptance suite, but not stated by any requirement. Both were
flagged as unruled in pull request #4 and are now ruled on by the maintainer.

### 1. Does a refused burst end the engagement?

`advance()` emits `FIRE_AT_TARGET`. `SafetyPolicy::authorise_fire` may then
refuse it — cool-down, engagement rate, exclusion zone, empty envelope,
unhealthy link. The state machine is a pure function and hears nothing back
(ADR-0007), so it has already returned to `SEARCHING` and retired the engaged
track by the time the refusal exists.

The implemented behaviour therefore *had* to be "the track is retired anyway",
but for a structural reason rather than a specified one. That is a poor footing
for a safety behaviour: it is true by accident of composition, and a later
refactor that let the machine observe the outcome would silently change it.

### 2. How long does `TARGET_LOST` last?

`REQ-TRK-005` said the system "SHALL then return to `SEARCHING`". The
implementation spent one whole transition in `TARGET_LOST`, unconditionally
emitting `KEEP_SEARCHING` and ignoring whatever the frame contained. A bird
confirmable in that frame was discarded and had to be confirmed again.

Neither the requirement nor the headers said whether that frame should be
looked at. The suite asserted the implemented reading
(`LeavesTargetLostForSearchingOnTheNextFrame`), which made the gap invisible:
the test documented the behaviour without anyone having decided it.

## Decision

**1. An engagement ends when the state machine issues the fire intent, whether
or not the command is authorised or transmitted.** A refused burst retires the
engaged track exactly as a transmitted one does. `REQ-TRK-012` now says so.

This deliberately differs from ADR-0011, and the two are not in tension. ADR-0011
governs the **rate limiter**: a burst refused by `SafetyPolicy` was never
transmitted, so it does not consume a cool-down or an engagement slot. This ADR
governs the **track**: the same refused burst does retire it. One asks "did
water possibly leave the nozzle?", the other asks "has this bird already had
its turn?". A refusal answers no to the first and yes to the second.

This is the conservative direction and it is now deliberate. The alternative —
crediting the confirmation back when the burst was refused — means the bird is
re-locked and re-aimed on every frame throughout a two-second cool-down it is
forbidden to fire in, which is servo wear for no deterrent, and it makes
`REQ-SAF-002`'s "for the current engagement" approximately true rather than
literally true.

**2. `TARGET_LOST` does not persist.** It occupies exactly one transition. The
frame that leaves it is judged as any other searching frame: the system returns
to `SEARCHING`, or enters `TARGET_LOCKED` directly if that frame confirms a
target. `REQ-TRK-005` now says so, and `advance()` shares one code path between
`SEARCHING` and `TARGET_LOST`.

Losing a target costs the frame that observed the loss, and no more. The
previous behaviour discarded a further confirmable frame — 200 ms at the 5 FPS
of `REQ-DET-003` — for no safety benefit. A track confirmed in that frame
earned its three consecutive detections on its own (`REQ-TRK-002`) and still
owes a verification frame (`REQ-TRK-004`) before anything can fire, so nothing
about `REQ-SAF-002` weakens.

The state is still stored for one transition rather than collapsed away. That
was the explicit choice: collapsing it would make `REQ-TRK-006` vacuous, would
leave `REQ-TRK-001`'s third state unreachable, and would delete the
`TARGET_LOCKED → TARGET_LOST → SEARCHING` sequence that `REQ-TRK-005` has
always been verified by.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Credit the confirmation back when a burst is refused | The state machine cannot see the refusal without being given the outcome, which destroys its purity (ADR-0007) and with it the exhaustive testability that `REQ-SAF-002` rests on. It also re-locks and re-aims every frame through the cool-down. |
| Collapse `TARGET_LOST` entirely, so it is never stored | Considered and put to the maintainer explicitly. It makes `REQ-TRK-006` vacuous, `REQ-TRK-001`'s third state unreachable, and contradicts the `REQ-TRK-005` acceptance sequence that has been verified since the suite was written. Rejected in favour of keeping the state observable for one transition. |
| Leave `TARGET_LOST` costing a frame, and document it | Documents an accident. Nobody chose the frame's loss; it fell out of writing the case as an unconditional return to `SEARCHING`. |
| Let `TARGET_LOST` last until a `NONE` frame arrives | Unbounded, and re-introduces exactly the "how stale is too stale?" question ADR-0016 rejected for link health. |

## Consequences

### Positive

* Both behaviours are now decisions with requirements behind them, rather than
  readings that happened to be implemented.
* `advance()` gets smaller: `SEARCHING` and `TARGET_LOST` share one code path,
  which is the honest expression of "the state is left immediately".
* Recovery after a lost target is one frame quicker in the case where another
  bird is already confirmable.
* The refusal path's conservatism is stated where a reader looks for it, so a
  future change that would credit confirmation back has to argue with a
  requirement rather than merely not notice one.

### Negative / accepted trade-offs

* A bird whose burst was refused must earn three fresh detections, so some
  deterrent opportunities are missed that a crediting design would take. This
  is the same trade-off `REQ-TRK-012` already accepted for transmitted bursts,
  now extended explicitly to refused ones.
* `TARGET_LOST` is now a state the system passes through rather than rests in,
  which makes it marginally harder to observe in a debugger or a log. It is
  still present in every transition it occurs in.
* One committed test asserted the old `TARGET_LOST` behaviour and has been
  rewritten. That is a specification change, not a weakened test: it now
  asserts the ruled behaviour, and gained a companion asserting the state never
  occupies two consecutive transitions.

## Verification

* `REQ-TRK-005` gained two acceptance criteria: a frame leaving `TARGET_LOST`
  that confirms a track yields `TARGET_LOCKED` in that same transition, and
  `TARGET_LOST` is never the state of two consecutive transitions.
* `REQ-TRK-012` gained one: a burst refused by `SafetyPolicy` retires the
  engaged track exactly as a transmitted burst does.
* The exhaustive reachability test in
  `tests/core/target_state_exhaustive_test.cpp` covers both rulings without
  modification, because it asserts the invariants rather than the paths:
  `TARGET_LOST` is still entered only from `TARGET_LOCKED` (`REQ-TRK-006`), and
  locking still requires a track confirmed in that very frame (`REQ-TRK-002`).
* `scripts/trace.sh` continues to require `REQ-TRK-005` and `REQ-TRK-012` to be
  covered by named tests, and the coverage ratchet in
  `docs/requirements/verified.txt` already lists both.
