# ADR-0013 — Ending an engagement retires its track

**Status:** Accepted
**Date:** 2026-09-16
**Affects:** `REQ-TRK-012`, `REQ-TRK-011`, `REQ-SAF-002`, `REQ-COM-002`,
`core/include/pigeon/core/target_state.hpp`,
`core/include/pigeon/core/track.hpp`

## Context

`REQ-SAF-002` forbids a fire command unless `REQ-TRK-002` and `REQ-TRK-004`
have both been satisfied **for the current engagement**. ADR-0012 ended an
engagement at the burst and allowed the same bird to be engaged again, and
ADR-0010 discards a track only when it misses a frame. Those two together left
a bird that is detected in every frame carrying its confirmation across the
burst: fired upon at a count of four, still confirmable at five on the very
next frame, re-locked and re-aimed immediately. The second engagement would be
authorised by detections counted towards the first — an inherited confirmation,
which is exactly what `REQ-SAF-002` exists to forbid.

The maintainer answered the question (Q16): the count is reset, after a burst
and after an abandonment alike. `REQ-TRK-012` now carries that.

That leaves a design question this ADR answers: **where** the reset is applied,
such that an application loop cannot fail to apply it. The counts live on
`Track` values, which the caller owns; `advance` is a pure function returning a
new state. A reset that the caller must remember to perform is not a safety
property, it is a hope.

## Decision

Two decisions, one mechanism.

**The reset is realised by retiring the track, not by zeroing its count.** When
an engagement ends, the engaged track is removed from the track set. The bird's
next detection starts a new track at a count of one and must earn
`confirmation_frame_count` fresh consecutive detections. This is the same
mechanism `REQ-TRK-009` already uses for a missed frame, for the reason
ADR-0010 gives: a retained track with a zeroed count and a brand-new track are
indistinguishable for confirmation purposes, and retirement keeps the invariant
that a live track's count is never zero.

**The carried track set lives inside `TargetMachineState`.** The state value a
caller carries from frame to frame holds the tracks and the identity allocator
alongside the state and the engaged track. `associate_detections` takes its
previous tracks from `TargetMachineState::tracks`, its output travels to
`advance` through `FrameInput`, and the set for the next frame comes back as
`TargetTransition::next` — already retired. `abandon_engagement` returns the
same whole state, so the `REQ-COM-002` path retires too.

The cycle is therefore one-way and passes through the state machine:

```text
state.tracks ─▶ associate_detections ─▶ TrackUpdate ─▶ FrameInput
       ▲                                                    │
       └───────────── advance(state, input).next ◀──────────┘
```

Retiring is skipped only by a caller that deliberately keeps a second copy of
the state and feeds the older one back. It cannot be reached by omitting a
step, because there is no step to omit: the loop's single assignment
`state = transition.next;` carries the retirement.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Zero the engaged track's count and keep the track | Breaks the invariant that a live track's count is at least one, which both `REQ-TRK-009` and the `Track` documentation rely on, and reintroduces the zeroed-track state ADR-0010 removed. Behaviourally identical otherwise, by ADR-0010's own argument. |
| Leave the tracks with the caller and have `TargetTransition` carry a `reset_confirmation_for` field | The caller must notice the field and act on it. Forgetting produces exactly the unearned second burst this decision exists to prevent, and nothing in the type system objects. |
| Seal the carried track set in a type constructible only by `advance` | Structurally airtight, and rejected because it makes state-machine fixtures unconstructible: every test would have to drive the machine through association to reach a state. ADR-0007 values `TargetMachineState` being a plain value a test can build directly, and enumerable inputs are what make `REQ-SAF-002` checkable by exhaustion. |
| Fold association into `advance`, so one function owns both | Pulls the association radius — configuration — into the pure transition function, which ADR-0005 and ADR-0007 deliberately kept free of configuration, and merges two seams that are currently testable apart. |
| Reset only after a burst, not after an abandonment | The maintainer chose the consistent rule. An abandoned engagement earned no less and no more than a fired one; a second rule would be a second thing to remember. |

## Consequences

### Positive

* `REQ-SAF-002` becomes literally true: every fire command is authorised by
  detections counted towards that engagement and no other.
* No responsiveness is lost. Three fresh detections at 5 FPS (`REQ-DET-003`) is
  600 ms, inside the 2 s cool-down (`REQ-SAF-005`) the system must wait out
  anyway.
* Less pointless actuation: the rig no longer re-locks and re-aims on every
  frame at a bird it is forbidden to fire at. That is servo wear avoided.
* The caller carries one value instead of three, and the application loop
  becomes `state = advance(state, input).next;`.
* Retirement reuses the discard mechanism, so there is one way a track leaves
  the set, not two.

### Negative / accepted trade-offs

* **Opportunities will be missed.** If a bird is detected intermittently, the
  count restarts and re-confirmation can take longer than the cool-down, so
  bursts a non-resetting design would have delivered will not happen. Accepted
  deliberately: a missed deterrent is cheap, an unearned burst is not.
* A burst refused by `SafetyPolicy` — cooling down, rate limited, exclusion
  zone — also retires the track, because the pure state machine cannot hear the
  refusal (ADR-0007). A refused engagement therefore costs three frames of
  re-confirmation. This is derived from the intent/command split rather than
  specified, and it errs towards firing less.
* `TargetMachineState` now owns a `std::vector<Track>`, so copying a state
  copies the tracks and the type is no longer trivially comparable. At a
  handful of tracks and 5 FPS this is negligible against the 1 GB budget.
* `advance` carries `tracks` and `next_id` through without consulting them.
  That redundancy is the price of the caller having a single value to hold.

### Follow-up work

* The application loop, still to be designed, should hold exactly one
  `TargetMachineState` and no separate track container. A second container is
  the only way to defeat this decision, so a reviewer has one thing to look
  for.

## Verification

* `REQ-TRK-012` is a declared requirement, so `scripts/trace.sh` reports it
  `UNVERIFIED` until a test cites it, and `scripts/check.sh` runs `trace.sh`.
* Directly testable as a pure sequence, with no clock and no hardware: feed one
  track detected in every frame, assert `FIRE_AT_TARGET` on the verification
  frame, then assert the two following frames are `SEARCHING` with
  `KEEP_SEARCHING` and no `aim_at`, and that the third yields
  `AIM_AT_TARGET` again.
* The retirement itself is observable in the returned value: the engaged
  `TrackId` is absent from `TargetTransition::next.tracks`, and the same bird's
  next detection carries a new identifier with a count of one.
* The `REQ-COM-002` path is tested the same way through `abandon_engagement`.
