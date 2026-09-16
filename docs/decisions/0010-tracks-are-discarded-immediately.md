# ADR-0010 — A track that misses a frame is discarded immediately

**Status:** Accepted
**Date:** 2026-09-16
**Affects:** `REQ-TRK-003`, `REQ-TRK-007`, `REQ-TRK-009`,
`core/include/pigeon/core/track.hpp`

## Context

`REQ-TRK-003` requires the confirmation counter to reset when a frame contains
no pigeon. ADR-0003 settled how detections associate with tracks. Neither says
what becomes of a track that survives a frame without receiving a detection:
it could be retained with a zeroed count for some number of frames, in the
hope the bird reappears and resumes its identity, or it could be dropped at
once.

Retention is the conventional choice in multi-object tracking, and it is worth
saying why it is not the right choice here.

Retention would change an outcome only if a retained-but-zeroed track behaved
differently from a brand-new track on re-association. It does not. The retained
track resumes at a count of one; a new track starts at a count of one. Both
need `confirmation_frame_count` further consecutive frames to confirm
(`REQ-TRK-002`). The only observable difference is the `TrackId`, and the only
place an identifier's continuity matters is the engaged track during
verification — a case already decided, on the same frame, by ADR-0009 and
`REQ-TRK-010`, which declares the target lost.

Retention therefore buys nothing while costing a configured parameter, a decay
path, and a family of tests for how long a ghost track lingers.

## Decision

A track that receives no detection in a frame is **discarded** at the end of
that frame. There is no retention window, no decay count and no configured
parameter governing either. A `NONE` frame therefore empties the track set.

Two invariants follow, and the interface documents both: every track in a
track set was detected in the frame just processed, and
`Track::consecutive_detections` is never zero.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Retain the track with a zeroed count for N frames | Provably equivalent for confirmation purposes, as above. It adds a parameter that cannot be tuned by observing any behaviour that depends on it. |
| Retain the track **and** its count, so a one-frame gap does not reset | Directly contradicts `REQ-TRK-003`, which requires the reset. It would also let an intermittently detected object — foliage moving in and out of a threshold — confirm a target over time. |
| Retain the engaged track only | The one case where identity matters is resolved on the same frame by `REQ-TRK-010`. Retaining a track solely to discover it is already lost is state with no reader. |

## Consequences

### Positive

* The counter reset of `REQ-TRK-003` is realised in the strongest available
  form: the track ceases to exist, so no stale count can be read.
* `associate_detections` has no notion of frame age, so it stays a pure
  function of the detections and the previous track set (`REQ-DEV-002`).
* One fewer configured parameter, and no ghost-track lifetime to test.
* Memory is bounded by the number of birds visible **now**, which matters on a
  1 GB device running for months.

### Negative / accepted trade-offs

* A bird whose detector output flickers for a single frame loses its identity
  and must accumulate three fresh consecutive detections before it can be
  confirmed again. At 5 FPS (`REQ-DET-003`) that is roughly 600 ms of delay.
  Accepted: `REQ-TRK-002` exists precisely to trade responsiveness for
  certainty.
* If detector flicker turns out to be common in the field, the remedy is a
  better detector or a lower detection threshold, not a retention window that
  would launder the flicker into a confirmation.

### Follow-up work

* None. If field evidence later argues for retention, it supersedes this ADR
  and must show a behaviour that retention changes.

## Verification

* `REQ-TRK-009` is a declared requirement, so `scripts/trace.sh` reports it
  `UNVERIFIED` until a test cites it.
* Directly testable: associate a detection, then pass an empty detection set,
  and assert the returned `TrackUpdate::tracks` is empty; then re-associate a
  detection at the same position and assert the new track has a fresh
  `TrackId` and a count of one.
* The "Configured parameters" table in `docs/requirements/requirements.md`
  records track retention as deliberately absent, so a parameter appearing
  there later is a visible change of this decision rather than a quiet one.
