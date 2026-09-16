# System Architecture

The system is designed to detect pigeons using computer vision, confirm and track detections, calculate the required pointing direction, and control a water-based deterrent.

The software is being developed and tested independently of the physical hardware. The eventual deployment target consists of a Raspberry Pi and an Arduino.

## Target Hardware Architecture

### Raspberry Pi

The Raspberry Pi 3 Model B, with 1 GB of RAM, will be responsible for high-level processing.

A camera connected to the Raspberry Pi will provide image frames to an object detection model running locally on the device.

The Raspberry Pi will be responsible for:

1. Capturing camera frames.
2. Running object detection.
3. Confirming pigeon detections across multiple frames.
4. Tracking confirmed pigeons.
5. Calculating the target position relative to the deterrent.
6. Converting the target position into X and Y servo angles.
7. Sending the required angles to the Arduino.

The Raspberry Pi's 1 GB of RAM is a significant resource constraint. Memory usage should therefore be considered when selecting object detection models, image-processing pipelines, and runtime architectures.

### Arduino

The Arduino will be connected to the Raspberry Pi via USB.

It will be responsible for low-level physical control:

* Receiving commands from the Raspberry Pi.
* Moving the X and Y servo motors.
* Positioning the deterrent according to the requested angles.
* Controlling the water actuator.

The communication protocol between the Raspberry Pi and Arduino is a defined system boundary and should be documented separately.

## Detection and Confirmation

An object detection in a single camera frame is not sufficient to consider a pigeon confirmed.

Detections are associated across frames into **tracks**: each detection is
matched to the nearest existing track whose centroid lies within a configured
association radius, and an unmatched detection starts a new track. A pigeon is
confirmed only when **the same track** has been detected in three consecutive
frames (ADR-0003).

Conceptually:

```text
Frame 1: pigeon detected  ─┐
        ↓                   │ all associated to one track
Frame 2: pigeon detected  ─┤ (centroids within the association radius)
        ↓                   │
Frame 3: pigeon detected  ─┘
        ↓
Confirmed target
```

Three detections that cannot be associated into a single track do **not**
confirm a target: that is three birds passing through, not one bird sitting
still.

When several tracks are confirmable at once, exactly one is engaged — the
largest by bounding-box area, with deterministic tie-breaking
(`REQ-TRK-008`). Pigeons are gregarious, so this case is the norm rather than
an exception.

The detection model itself remains an implementation detail and stays
independent of the camera and hardware interfaces.

## Targeting

Once a pigeon has been confirmed, the system converts the selected track's
centroid into the X and Y angles required to point the deterrent at it.

**The camera is mounted on the pan/tilt rig, boresighted with the nozzle**
(ADR-0004). Camera and nozzle therefore point the same way and move together,
so a target's angular offset from the image centre *is* the correction the
servos must apply:

```text
angle_x = neutral_x + boresight_x + ((cx - width/2)  / width)  * HFOV
angle_y = neutral_y + boresight_y - ((cy - height/2) / height) * VFOV
```

This is deliberately depth-independent: no range estimate to the bird is
required, which is what makes single-camera aiming tractable at all.

Coordinate conventions:

| Frame  | Convention                                                          |
| ------ | -------------------------------------------------------------------- |
| Image  | Origin top-left, +x right, +y **down**.                              |
| Servo  | X = 0° straight ahead, +X right. Y = 0° horizontal, +Y elevated.     |

The sign flip on the Y term is the handedness change between the two frames.

Calibration — fields of view, boresight offset, neutral angles, mechanical
envelope and association radius — lives in a version-controlled key/value file,
is parsed in `raspberry/`, and reaches `core/` as a plain value type
(`REQ-AIM-003`). The default envelope is empty, so an uncalibrated system is
inert rather than dangerous.

## Hardware Independence

**The current development phase does not depend on physical hardware.**

The complete system must be buildable, runnable, simulated, and tested on macOS without:

* Raspberry Pi
* Arduino
* Physical camera
* Servo motors
* Water actuator
* GPIO hardware

Hardware-dependent components must be isolated behind software interfaces so they can be replaced by simulated implementations during development.

The intended development pipeline is:

```text
Simulated camera / test images
          ↓
   Object detection
          ↓
      Tracking
          ↓
 Detection confirmation
          ↓
      Targeting
          ↓
  Simulated communication
          ↓
   Simulated servos
          ↓
 Simulated water actuator
```

The physical Raspberry Pi and Arduino implementations will be introduced later without requiring changes to the core application logic.

## Architectural Principle

The system is organized around three main responsibilities:

* **Vision** — captures and processes images, detects pigeons, and confirms and tracks detections across frames.
* **Targeting** — determines the relative position of a confirmed pigeon and calculates the X and Y angles required to point the deterrent.
* **Control** — sends targeting commands to the actuator system and, eventually, controls the physical servos and water actuator through the Arduino.

These responsibilities should remain loosely coupled through well-defined interfaces.

The core application must remain hardware-independent so that the complete pipeline can be simulated and tested on macOS.

## The `core/` seams

`core/` is cut along the boundary between **deciding** and **doing**
(ADR-0007). Each header below is one seam; each is hardware-free, and the three
hardware-shaped ones are interfaces with simulated implementations (ADR-0001).

| Header                    | Responsibility                                                                                     | Serves                                          |
| ------------------------- | --------------------------------------------------------------------------------------------------- | ----------------------------------------------- |
| `geometry.hpp`            | Image-plane vocabulary: `PixelPoint`, `BoundingBox`, `ImageSize`, `PixelDistance`.                    | `REQ-TRK-007`, `REQ-TRK-008`, `REQ-AIM-001`      |
| `frame.hpp`               | `FrameView`: a borrowed, non-owning view of one packed RGB888 frame. No camera type exists in `core/`. | `REQ-DET-002`, `REQ-DET-004`, `REQ-DEV-001`      |
| `detection.hpp`           | `DetectionResult` (`NONE`/`FOUND`), `Detection`, and `DetectionOutcome`, which is `FOUND` if and only if it carries a detection. | `REQ-DET-001`                    |
| `detector.hpp`            | `Detector`: the seam to the model runtime. Total, deterministic, stateless across frames.             | `REQ-DET-001`, `REQ-DET-002`                     |
| `track.hpp`               | `TrackId`, `Track`, and the pure functions `associate_detections` and `select_confirmed_target`. A track missed for one frame is discarded; an engaged track is retired when its engagement ends. | `REQ-TRK-002`, `REQ-TRK-007`…`REQ-TRK-009`, `REQ-TRK-012` |
| `target_state.hpp`        | The pure transition function `advance`, over `TargetMachineState` and `FrameInput`, emitting an intent. `TargetMachineState` carries the track set, so the confirmation reset cannot be skipped. | `REQ-TRK-001`…`REQ-TRK-006`, `REQ-TRK-010`…`REQ-TRK-012`, `REQ-SAF-002` |
| `aiming.hpp`              | The boresighted transform, `Angle`/`ServoAngles`, and the empty-by-default `AngleRange` and envelope.  | `REQ-AIM-001`, `REQ-AIM-002`                   |
| `configuration.hpp`       | `Configuration`: a plain value type, parsed in `raspberry/`, inert by default. Carries at most one exclusion zone. | `REQ-AIM-003`, `REQ-SAF-001`, `REQ-SAF-003`, `REQ-SAF-005`, `REQ-SAF-006` |
| `clock.hpp`               | `MonotonicClock`: the only way `core/` can learn the time.                                            | `REQ-SAF-005`, `REQ-DEV-002`                     |
| `actuator_link.hpp`       | `ActuatorLink`: the device boundary, with failure as a returned `LinkStatus`.                         | `REQ-COM-001`, `REQ-COM-002`, `REQ-SAF-004`      |
| `safety_policy.hpp`       | `SafetyPolicy`: clamp, exclusion zone, cool-down, rate limit, bounded burst. Every transmitted command counts. | `REQ-SAF-001`…`REQ-SAF-007`, `REQ-AIM-002`       |

The pipeline reads as a chain of values, with state carried by the caller:

```text
FrameView ─▶ Detector ─▶ DetectionOutcome
                            │
                            ▼
      associate_detections(state.tracks, …) ─▶ TrackUpdate
                            │
                            ▼
                   FrameInput ─▶ advance(state, input) ─▶ TargetTransition
                            ▲                                │ intent
   state = transition.next ──┘                               ▼
   (tracks carried back, with any retirement applied)
                        aim_at_centroid ─▶ SafetyPolicy ─▶ FireAuthorisation
                                                             │
                                                             ▼
                                                        ActuatorLink
```

Three properties of this shape matter more than its details:

* **The fire path crosses three independent components.** The state machine
  says the bird is there, the safety policy says firing at it is permitted, and
  the link says the device accepted it. None of them can fire alone
  (`REQ-SAF-002`).
* **The state machine emits an intent, not a command.** Clamping, the
  exclusion zone and rate limiting happen after it, which is what keeps it a
  pure function with no configuration and no clock (ADR-0005).
* **Identity is checked at every step of the fire path.** One track confirms
  (`REQ-TRK-002`), the same track must be re-detected to verify
  (`REQ-TRK-010`, ADR-0009), and that is why `FrameInput` carries tracks rather
  than a bare classification. The engagement then ends at the burst and the
  machine returns to `SEARCHING`, with repetition bounded by the rate limiter
  alone (`REQ-TRK-011`, ADR-0012).
* **Failure is a value.** `LinkStatus`, `FireRefusal` and `std::optional`
  cross module boundaries; exceptions do not.
* **The caller carries one value, and it is the state machine's.**
  `TargetMachineState` holds the track set and the identity allocator as well
  as the state, because the confirmation counters are safety state: ending an
  engagement retires the engaged track (`REQ-TRK-012`), and the only track set
  a caller has to hand to the next frame is the one the machine gave back, with
  the retirement already applied (ADR-0013).

`ActuatorLink` is the *operation-level* half of the device boundary. Its wire
encoding — framing, checksums, replies — is still to be specified in this
directory (`REQ-COM-001`), and the interface deliberately says nothing about
it.

## Development Principles

These principles are the reason the architecture is shaped the way it is.
They were previously held in `AGENTS.md`; that file is now the agent operating
manual and this document owns the product description.

* Keep hardware-independent domain logic separate from hardware integration.
* Prefer interfaces and dependency injection at hardware boundaries.
* Every important behaviour must be testable without physical hardware.
* Provide simulations for hardware-dependent components where practical.
* Never introduce Raspberry Pi or Arduino dependencies into `core/`.
* Keep the system executable on macOS throughout development.
* Preserve clear boundaries between detection, tracking, targeting,
  communication and actuation.

### Module boundaries

| Module       | Responsibility                                                | May depend on          |
| ------------ | ------------------------------------------------------------- | ---------------------- |
| `core/`      | Detection contracts, target state machine, targeting maths, command generation | C++ standard library only |
| `raspberry/` | Camera capture, model runtime, serial host side               | `core/`                |
| `arduino/`   | Servo and water actuator firmware, hardware safety limits     | Arduino libraries only |
| `tests/`     | Verification of the above using simulated implementations     | `core/`, GoogleTest    |

The dependency direction is one-way into `core/`. A hardware header appearing
anywhere under `core/` is an architectural defect, not a style issue.

## Testing Strategy

All tests must run on macOS without hardware (`REQ-DEV-001`).

| Level        | Scope                                                        |
| ------------ | ------------------------------------------------------------ |
| Unit         | Individual algorithms and components in isolation            |
| Integration  | Several components wired together with simulated hardware    |
| Scenario     | Recorded frame sequences driven through the full pipeline    |
| End-to-end   | Simulated camera → detection → targeting → simulated actuator |

Fixtures must be deterministic: recorded images and scripted scenarios, never
live captures or unseeded randomness (`REQ-DEV-002`).

Hardware-in-the-loop testing will be introduced later and must remain separate
from the default suite and from CI (`REQ-DEV-003`).

## Safety Considerations

The deterrent is an actuated water jet operating autonomously outdoors. The
architecture must therefore treat firing as a guarded operation, not an
ordinary command:

* Angles are clamped to the mechanical envelope before transmission
  (`REQ-AIM-002`), and the default envelope is empty.
* Firing is prohibited inside a configured exclusion zone (`REQ-SAF-003`).
* Fire duration is bounded at 500 ms and self-terminating **in firmware**, so
  the bound survives a Raspberry Pi failure (`REQ-SAF-001`).
* Engagements are rate limited: a 2 s cool-down and at most 6 per minute
  (`REQ-SAF-005`).
* Loss of the actuator link abandons the engagement (`REQ-COM-002`).
* Startup and shutdown leave the actuator inactive (`REQ-SAF-004`).

Safety limits are enforced on **both** sides of the device boundary: the
Raspberry Pi must not request an unsafe action, and the Arduino must not
perform one even if requested.

### Where time lives

The target state machine counts **frames**, never milliseconds: verification
uses the immediately following frame and reads no clock, which keeps the
safety-critical component a pure function and its tests free of timing
(ADR-0005). Elapsed real time appears in exactly two places — the rate limiter,
through an injected monotonic clock, and the firmware's own burst timer.

In code, that injected clock is `MonotonicClock` (`core/clock.hpp`), and the
only component that holds one is `SafetyPolicy`. `advance`, the state
transition function, takes no clock and has no way to obtain one.
