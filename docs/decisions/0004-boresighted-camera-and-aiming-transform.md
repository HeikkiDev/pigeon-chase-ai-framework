# ADR-0004 — Boresighted camera and the camera-to-actuator transform

**Status:** Accepted
**Date:** 2026-09-15
**Affects:** `REQ-AIM-001`, `REQ-AIM-002`, `REQ-AIM-003`
**Resolves:** Open question Q3

## Context

`REQ-AIM-001` required "an explicitly documented camera-to-actuator transform"
without saying what the transform was, because the physical arrangement had
never been decided. Q3 asked for the coordinate frames and the calibration
procedure.

This is a software question with a hardware answer. Two arrangements were
possible, and they differ enormously in difficulty:

* **Fixed camera, moving nozzle.** The camera sees the world in one frame; the
  nozzle points in another. Converting a pixel to an angle requires the
  extrinsic transform between the two frames *and* the range to the target,
  because the parallax between camera and nozzle depends on distance.
  Monocular range estimation of a small, deformable, unknown-size object is
  unreliable, and the error feeds directly into the aim.

* **Camera boresighted on the pan/tilt rig.** The camera and the nozzle point
  the same way and move together. A target's angular offset from the image
  centre *is* the angular correction the servos must apply. No range, no depth,
  no parallax term.

## Decision

The camera is mounted on the pan/tilt rig, boresighted with the nozzle.

Aiming is:

```text
angle_x = neutral_x + boresight_x + ((cx - width/2)  / width)  * HFOV
angle_y = neutral_y + boresight_y - ((cy - height/2) / height) * VFOV
```

Calibration therefore reduces to four numbers plus a two-axis boresight offset:
the horizontal and vertical fields of view, the neutral angles, and the
mechanical envelope. These are recorded in a version-controlled key/value file,
parsed in `raspberry/`, and handed to `core/` as a plain value type
(`REQ-AIM-003`).

Coordinate conventions, stated once so that nothing downstream has to guess:

| Frame  | Convention                                                          |
| ------ | -------------------------------------------------------------------- |
| Image  | Origin top-left, +x right, +y **down**, as every image library does.  |
| Servo  | X = 0° straight ahead, +X right. Y = 0° horizontal, +Y elevated.      |

The sign flip on the Y term above is the image-to-servo handedness change, and
it is written out explicitly because getting it backwards aims the water at the
ground rather than the sky.

## Alternatives considered

| Option                                          | Why it was rejected                                                                                           |
| ----------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| Fixed camera, moving nozzle                     | Requires extrinsic calibration and a monocular range estimate. The range error dominates the aim error, and the calibration procedure is a project in itself. |
| Full pinhole model with lens distortion         | Needs a checkerboard calibration procedure and a matrix library. Distortion matters at the frame edges, where a water jet is least accurate anyway. Revisit if bring-up shows edge error is the limiting factor. |
| Stereo camera for range                         | A second camera, more RAM, more CPU, on a 1 GB board. The boresighted design removes the need for range entirely. |
| Hard-coded calibration constants in `core/`     | Puts a property of one physical rig into hardware-independent logic, and makes recalibration a recompile.        |
| JSON or YAML configuration                      | Needs a third-party parser, which needs its own ADR and costs RAM. Key/value is enough for a dozen scalars.      |

## Consequences

### Positive

* Aiming is depth-independent: a linear map from pixel offset to angle.
* Calibration is a handful of scalars, reviewable in a diff.
* The transform is trivially testable from fixtures with no hardware
  (`REQ-DEV-001`).
* Mechanical convention is documented once, in one place.

### Negative / accepted trade-offs

* The camera now moves, so it cannot observe the wider scene while aiming, and
  a target can leave the field of view *because the rig moved*. This is
  precisely what the `REQ-TRK-004` verification frame exists to catch.
* Cabling to a moving camera is a mechanical reliability concern.
* The linear FOV approximation ignores lens distortion, so accuracy degrades
  toward the frame edges. Acceptable for a water jet with a wide cone; not
  acceptable for anything needing precision.
* The boresight offset is uncalibrated until the rig is built. The empty
  default envelope (`REQ-AIM-002`) means an uncalibrated system fires at
  nothing rather than firing somewhere wrong.

## Verification

* `REQ-AIM-001` acceptance criteria assert centre and edge behaviour and
  explicitly forbid any range term.
* `REQ-AIM-003` acceptance criteria assert that `core/` performs no file access
  and that a malformed configuration leaves the system in the safe state.
