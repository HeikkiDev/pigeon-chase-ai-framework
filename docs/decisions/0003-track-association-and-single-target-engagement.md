# ADR-0003 — Track association and single-target engagement

**Status:** Accepted
**Date:** 2026-09-15
**Affects:** `REQ-TRK-002`, `REQ-TRK-007`, `REQ-TRK-008`, `REQ-AIM-001`
**Resolves:** Open questions Q1 and Q7

## Context

`REQ-TRK-002` originally required "three consecutive frames classified
`FOUND`". Read literally that is a statement about *frames*, not about *birds*,
and it has two consequences nobody intended:

* Three different pigeons passing through the frame in three successive frames
  confirm a target that was never persistently there.
* `REQ-AIM-001` must aim at "the target", but presence-only confirmation never
  identifies which detection that is. In a multi-bird frame the question has no
  answer at all.

Two open questions followed from this. Q7 asked whether detections should be
associated across frames. Q1 asked what to do when several pigeons appear at
once — engage one, or ignore the frame entirely.

The two are the same question seen from different sides: both are asking what,
exactly, the system is tracking.

## Decision

**Detections are associated into tracks.** Each detection is matched to the
nearest existing track whose centroid lies within a configured association
radius; an unmatched detection starts a new track. Confirmation requires three
consecutive detections of *one track*, not three frames containing *something*.

**Exactly one target is engaged at a time**, selected as the detection with the
largest bounding-box area, ties broken by ascending X then ascending Y centroid
coordinate.

Association is nearest-centroid with a radius gate — not a Kalman filter, not
Hungarian assignment, not a learned re-identification model. It stays a pure
function of the current tracks and the incoming detections, with no clock and
no randomness, so the state machine remains exhaustively testable
(`REQ-SAF-002`).

## Alternatives considered

| Option                                                    | Why it was rejected                                                                                                                             |
| --------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| Per-frame presence only (no association)                   | Simplest, but confirmation stops meaning "a pigeon stayed put" and aiming has no defined subject. The simplicity is paid for in correctness.       |
| Ignore any frame containing more than one detection        | Superficially the safe choice. Pigeons are gregarious, so multi-bird frames are the normal case: this would leave the system permanently idle. A deterrent that never fires is not a safe deterrent, it is a broken one. |
| Full multi-object tracking (Kalman, Hungarian assignment)  | Disproportionate. It adds state, tuning parameters and probably a dependency, against a 1 GB RAM budget, to solve a problem that a radius gate solves. |
| Engage every confirmed target in turn                      | Multiplies water use and engagement rate, and conflicts with `REQ-SAF-005`. Also makes the state machine's fire path non-obvious, which is the last place to want cleverness. |
| Select the highest-confidence detection rather than the largest | Confidence is a property of the model, which will be swapped. Bounding-box area is a property of the scene and is a reasonable proxy for proximity. |

## Consequences

### Positive

* Confirmation now means what everyone assumed it meant.
* `REQ-AIM-001` has an unambiguous subject: the selected track's centroid.
* Selection is deterministic, so scenario tests are reproducible
  (`REQ-DEV-002`).
* The association radius is a single, explainable tuning parameter.

### Negative / accepted trade-offs

* The state machine gains a collection of tracks, so it is no longer a pure
  function of one enum. It remains a pure function of (tracks, detections).
* Nearest-centroid association will swap identities when two birds cross. For
  this system that is harmless — both are targets — but it would not be
  acceptable in a counting or identification application.
* The association radius is uncalibrated until the rig exists, and a badly
  chosen radius degrades quietly: too small never confirms, too large merges
  two birds into one track. This needs a bring-up check, not just a unit test.
* "Largest is closest" is an assumption. A large distant bird and a small near
  one are indistinguishable by area alone.

## Verification

* `REQ-TRK-007` acceptance criteria cover association inside and outside the
  radius, and determinism.
* `REQ-TRK-008` acceptance criteria cover multi-track frames and tie-breaking.
* `REQ-TRK-002` acceptance now includes the negative case: three unassociable
  detections must not confirm.
