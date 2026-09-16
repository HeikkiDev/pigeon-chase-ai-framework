# ADR-0009 — The verification frame must re-detect the locked track

**Status:** Accepted
**Date:** 2026-09-16
**Affects:** `REQ-TRK-004`, `REQ-TRK-005`, `REQ-TRK-010`, `REQ-SAF-002`,
`core/include/pigeon/core/target_state.hpp`

## Context

`REQ-TRK-002` is written about **one track**: three consecutive detections of
the same bird confirm a target, precisely so that three different birds
passing through three different frames cannot confirm a target that was never
there. ADR-0003 carried that identity requirement into track association.

`REQ-TRK-004` and `REQ-TRK-005`, by contrast, were written about the **frame
classification**: the frame after locking is re-examined, and a `NONE` frame
loses the target. Read literally, they say nothing about identity, which
leaves a gap: if the locked bird leaves and a different bird enters on the
verification frame, the frame is `FOUND`, and a classification-only reading
would authorise the burst.

That gap is on the fire path. Between confirmation and the water leaving the
nozzle there is exactly one check, and this is it.

## Decision

The `REQ-TRK-004` verification frame is satisfied **only** when the engaged
track itself is among the tracks detected in that frame. Any other frame —
`NONE`, or `FOUND` on birds other than the engaged one — transitions
`TARGET_LOCKED` → `TARGET_LOST` with no fire command, exactly as a `NONE`
frame does.

This is recorded normatively as `REQ-TRK-010` and realised in the interface by
`FrameInput` carrying the detected `Track` values rather than a bare
`DetectionResult`, so `advance` can compare `TargetMachineState::engaged_track`
against them.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Any detection satisfies verification (the literal reading of `REQ-TRK-004`)  | Lets bird A confirm an engagement that bird B then authorises. It contradicts the reasoning behind `REQ-TRK-002` and sits badly with `REQ-TRK-008`'s one-target-at-a-time rule, under which "the target" is a specific bird, not "a bird". |
| Verification by proximity — any detection within the association radius of the aim point | Re-implements association a second time, with a second tuning parameter, inside the safety-critical pure function. Track association already answers "is this the same bird?" (ADR-0003); asking it twice, differently, invites the two answers to disagree. |
| Re-confirm from scratch: require three more consecutive detections before firing | Contradicts `REQ-TRK-004`'s "immediately following frame" and would make the rig fire roughly a second later, by which time the bird has usually moved. |

## Consequences

### Positive

* The identity rule is uniform: the same bird confirms, is aimed at, and is
  fired upon.
* A bird that leaves and is replaced within one frame produces `TARGET_LOST`
  and no burst, which is the safe outcome.
* `advance` remains a pure function of `(state, input)`: identity comparison is
  an equality test on a `TrackId`, needing no clock and no configuration.

### Negative / accepted trade-offs

* Strictly more misses: a bird that is re-detected but whose detection fails to
  associate — a fast turn, a partial occlusion — starts a new track and is
  treated as a loss. A missed deterrent costs nothing; a burst at the wrong
  target is the failure that matters.
* `FrameInput` must carry tracks, so the state machine's input is slightly
  larger than a classification enum. At 5 FPS and a handful of birds this is
  irrelevant to the 1 GB budget.

### Follow-up work

* The test suite for `advance` must include the `TARGET_LOCKED` + `FOUND` on a
  **different** track case explicitly. It is the whole point of this decision
  and is exactly the case a classification-only test would miss.

## Verification

* `REQ-TRK-010` is a declared requirement, so `scripts/trace.sh` reports it
  `UNVERIFIED` until a test cites it, and `scripts/check.sh` runs `trace.sh`.
* The behaviour is testable without hardware and without a clock: construct a
  `TargetMachineState` in `TARGET_LOCKED` with a known `TrackId`, feed a
  `FrameInput::found` containing a different `TrackId`, and assert
  `TARGET_LOST`, `ABANDON`, and an absent `aim_at`.
* `scripts/arch-check.sh` keeps the decision cheap to test by keeping the state
  machine free of I/O and of clock reads.
