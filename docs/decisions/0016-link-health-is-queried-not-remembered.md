# ADR-0016 — Link health is queried at the decision, not remembered by the caller

**Status:** Accepted
**Date:** 2026-09-16
**Affects:** REQ-COM-003, REQ-SAF-008, REQ-COM-002, REQ-COM-001, `core/include/pigeon/core/actuator_link.hpp`, `core/include/pigeon/core/safety_policy.hpp`

## Context

`REQ-SAF-008` (ADR-0015) requires that only a healthy link may fire, and that
every link status which is not `OK` refuses the burst with its own distinct
reason. `LinkStatus` has four values.

`ActuatorLink`, as first designed, offered exactly one pre-send query:
`bool is_available() const noexcept`. A bool has two values. So a caller
standing at a fire decision could construct `OK` or `UNAVAILABLE` and nothing
else. `TRANSPORT_FAILURE` and `REJECTED` existed only as return values of
`send_*` — the outcome of an exchange that had already happened.

Nor could such an outcome reach a fire decision by another route. The exchange
immediately preceding any fire decision is the aiming command of the locking
frame; if that faults, `REQ-COM-002` abandons the engagement and `REQ-TRK-012`
retires the track, so there is no firing frame after it.

The result was a safety rule that was correct and unreachable: two of its four
cases could not occur in any honest composition of these interfaces. The
test-engineer could cover those branches only through a seam it invented inside
its own fixtures — `set_observed_link_status` — and said so rather than
presenting it as coverage. A rule that only a test double can trigger is not
enforced by anything.

## Decision

**Widen the device boundary so the rule can execute, rather than narrow the
rule so it stops asking.**

1. `ActuatorLink::health() const noexcept` returns a full `LinkStatus`,
   answerable at any time, before any command has been sent.
2. `bool is_available()` is **removed**, not retained alongside. Two queries
   about one property are two sources of truth, and two sources of truth are
   how they come to disagree. "Is the link usable?" is now
   `health() == LinkStatus::OK`.
3. `SafetyPolicy::authorise_fire` takes `const ActuatorLink&` in place of a
   `LinkStatus`. **The policy asks; the caller does not tell.**
4. `health()` must be answerable **without an exchange** — it transmits
   nothing, blocks on nothing and changes nothing on either device.

Point 3 is what makes the rule structural rather than documented. There is no
status parameter to fabricate, no status parameter to forget, and no way to
obtain an authorisation without the link having been consulted, because the
only code that can see whether it was consulted is the code that does the
consulting. It is the same move as putting the track set inside
`TargetMachineState` (ADR-0013): put the obligation where it cannot be skipped.

Point 4 avoids reintroducing the circularity from the other side. If learning
whether you may talk to the device required talking to the device, the query
would be worthless at exactly the moment it matters.

### What the firmware must do

Nothing new, and nothing on demand. `health()` is answered entirely on the
commanding side, from state the transport already holds: is the port open, did
the last exchange fault, has the device been heard from within the expected
heartbeat interval. The Arduino's obligations — already implied by
`REQ-COM-001` and now written down there — are:

* **Reply to every command**, distinguishing acceptance from refusal, so a
  `REJECTED` is a fact the host holds rather than a guess.
* **Emit a periodic heartbeat**, so that silence is detectable as
  `UNAVAILABLE` or `TRANSPORT_FAILURE` without the host sending anything to
  find out — in particular, without sending a fire command to discover that it
  should not have.

Both are ordinary serial-transport behaviour, cost the firmware a timer and a
status byte, and add nothing measurable to the Raspberry Pi 3B's 1 GB budget.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Redefine `link_status` as "the remembered outcome of the last exchange", and specify its persistence and clearing | The maintainer's explicit rejection, and the right one. Remembered state that nothing forces the caller to refresh is precisely the obligation an implementer forgets, and a safety rule resting on caller bookkeeping nobody checks is not a safety rule. It also has no correct answer at startup, when there is no last exchange, and no defined decay: how stale is too stale? Every one of those questions is a new gap. |
| Narrow `REQ-SAF-008` to the two statuses a bool can express | Fits the interface to its defect. `TRANSPORT_FAILURE` and `REJECTED` are real conditions of a real link, and a rig that fires over a link whose last exchange faulted is the hazard the requirement exists to prevent. |
| Keep `is_available()` beside `health()` for callers that want the simple question | Two overlapping queries about one property. An implementation can make them disagree, and a caller has no way to know which one the safety rule uses. The simple question is one comparison away. |
| Add `LinkHealth`, an unfabricable token obtainable only from a link, and keep passing a value into `authorise_fire` | Keeps the policy a function of values, but a token is a *snapshot*: fetched on one frame and passed on another, it is the remembered state this ADR rejects, merely wearing a type that makes it look fresh. Staleness would become unrepresentable-looking and entirely possible. |
| Fold transmission into `SafetyPolicy`, so it authorises, sends and records in one operation | This would close the residual hole below, but it merges the guard with the actuator, makes the decision non-`const`, and destroys the separation that lets `record_fire_sent` count transmissions rather than permissions (ADR-0011). Too large a change for the hole it closes. |
| A new enumeration for health, distinct from exchange outcomes | A second source of truth about the same four conditions, needing a second mapping to refusals and a second thing to keep exhaustive. The values already name every condition that matters. |

## Consequences

### Positive

* `REQ-SAF-008` now has four reachable cases instead of two. Every refusal
  reason can be produced by a real link reporting a real condition.
* A caller cannot obtain a fire authorisation against a health nobody observed,
  cannot substitute a stale one, and cannot omit the check.
* One question about link health has one answer, in one place.
* A degrading link is caught **before** a burst is sent, rather than being
  discovered by sending one.
* `noexcept` and `const` on `health()` state the no-exchange obligation in the
  type system, where a reviewer sees it.

### Negative / accepted trade-offs

* **The residual hole, stated plainly:** nothing binds the link consulted to
  the link commanded. A caller holding two links could ask one and send over
  the other. This is not closable without folding transmission into the policy,
  which costs more than it buys; a rig has one link, and the composition that
  would break this is one nobody has a reason to write.
* `health()` is a snapshot of the present, not a promise about the next
  millisecond. The link may still fail on the very next command, which is why
  every operation still returns a `LinkStatus` and `REQ-COM-002` still governs
  what comes back.
* Every implementation of `ActuatorLink` must now maintain enough transport
  state to answer without asking. For the simulated link this is a member; for
  the serial link it is bookkeeping it needed anyway.
* The committed acceptance suite was written against the old signature and no
  longer compiles. That is the correct signal — the contract changed — and it
  is the test-engineer's to repair, not the architect's.

### Follow-up work

* The test-engineer's `set_observed_link_status` seam in
  `tests/core/support/engagement_loop.hpp` is now unnecessary and should be
  removed: the condition it faked is expressible on the link itself.
* `scripts/arch-check.sh` does not yet enforce ADR-0015's "no `default:` in
  `refusal_for`" rule (defect D3). Referred to the maintainer.
* The wire protocol remains unspecified (`REQ-COM-001`). This ADR adds two
  obligations it must satisfy; it does not design it.

## Verification

* `REQ-COM-003` carries acceptance criteria a test can assert directly: each
  status is obtainable before any command is sent and reaches the fire
  decision; querying health transmits nothing, asserted against a simulated
  link that counts its transmissions; health degrading between the aiming frame
  and the firing frame refuses the burst with no intervening command.
* `REQ-SAF-008` gained a **reachability** bullet, so "every status refuses" is
  proven by a link reporting that status rather than by a fixture asserting it
  in principle.
* The signature enforces the rest: `authorise_fire` cannot be called without an
  `ActuatorLink`, so a compiling caller is a caller that consulted one. The
  compiler is the check, and it runs on every build.
* `scripts/trace.sh` requires `REQ-COM-003` to be covered by a named test.
