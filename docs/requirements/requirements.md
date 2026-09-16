# System Requirements

This document is the **authoritative specification** of what the system must do.

Every requirement has a stable ID. Implementation and tests reference these IDs,
and `scripts/trace.sh` maps each requirement to the tests that verify it.

## How to read this document

Each requirement has:

| Field          | Meaning                                                         |
| -------------- | --------------------------------------------------------------- |
| **ID**         | Stable identifier, e.g. `REQ-DET-001`. Never reuse or renumber.  |
| **Statement**  | Normative behaviour, using SHALL / SHALL NOT.                    |
| **Rationale**  | Why the requirement exists.                                      |
| **Acceptance** | Objectively checkable criteria a test can assert.                |
| **Superseded by** | Present only on retired requirements. Names the replacement.  |

### Requirements have no status field

Two questions could be asked of any requirement: *is it agreed?* and *is it
built?* Neither is recorded here, because both are already recorded somewhere
that cannot be edited to say something untrue:

| Question      | Answered by                                                             |
| ------------- | ------------------------------------------------------------------------ |
| Is it agreed? | **Its presence in this document.** This file is the authoritative specification, and changes to it are reviewed. If it is here, it is binding. |
| Is it built?  | **The tests.** `scripts/trace.sh` derives the verified set from the tests and prints it. |

A status field would restate those facts in a mutable place that nothing
validates, and a restated fact eventually contradicts the original. Worse, it
makes the damage cheap: promoting a requirement by editing one word is a
one-line diff that is easy to miss, whereas adding a whole requirement is a
conspicuous one. Removing the field makes this document harder to corrupt, not
easier (ADR-0006).

Consequences worth knowing:

* A requirement with no test is **outstanding work**, reported by the gate as
  `UNVERIFIED` but not a failure. Nobody has claimed it works.
* A requirement that was verified and no longer is, is a **coverage
  regression** and fails the gate. That is what deleting a test to get green
  looks like from the outside. The verified set lives in `verified.txt` and
  only ratchets upwards.
* Nothing that is still under discussion belongs in this document. Put it in
  [Open questions](#open-questions), or leave it in an unmerged pull request.

### Retiring a requirement

Never delete a requirement and never reuse its ID. Add a `**Superseded by:**`
line naming the replacement:

```markdown
### REQ-TRK-002 — Older behaviour

**Superseded by:** REQ-TRK-009
```

The gate checks that the successor exists, and allows the retired requirement
to drop out of the verified set.

ID prefixes:

| Prefix | Area                                 |
| ------ | ------------------------------------ |
| `DET`  | Detection                            |
| `TRK`  | Target state machine and tracking    |
| `AIM`  | Targeting and angle calculation      |
| `COM`  | Raspberry Pi ↔ Arduino communication |
| `ACT`  | Actuation (servos, water)            |
| `SAF`  | Safety                               |
| `DEV`  | Development environment constraints  |

## Rules for changing this document

* Agents **must not** invent requirements. If behaviour is unspecified, raise an
  open question in the [Open questions](#open-questions) section and stop.
* Changing an approved requirement requires an ADR in `docs/decisions/`.
* Never delete a requirement; add a `**Superseded by:**` line naming the
  replacement.

---

## Detection

### REQ-DET-001 — Frame classification

**Statement:** The detection component SHALL classify every camera frame it
receives as exactly one of `NONE` or `FOUND`.

**Rationale:** A single, closed result set keeps the downstream state machine
total and testable.

**Acceptance:**

* Given a fixture frame containing a pigeon, the detector returns `FOUND`.
* Given a fixture frame containing no pigeon, the detector returns `NONE`.
* The detector never returns any other value and never throws for a well-formed
  frame.

### REQ-DET-002 — Detector is independent of image acquisition

**Statement:** The detection component SHALL accept frames through an interface
and SHALL NOT depend on any camera, GPIO or platform-specific API.

**Rationale:** Enables deterministic testing from recorded fixtures
(`REQ-DEV-002`).

**Acceptance:**

* `core/` contains no include of a camera, GPIO, Raspberry Pi or Arduino header.
* The full detection path can be exercised from files on disk.

### REQ-DET-003 — Detection throughput

**Statement:** On the deployment target (Raspberry Pi 3B, 1 GB RAM) the
detection pipeline SHALL process at least **5 frames per second**.

**Rationale:** Confirmation needs three consecutive frames and verification
needs a fourth, so the engagement latency is roughly four frame periods. At
5 FPS that is under a second, which is fast enough to still find the bird where
it was seen.

**Acceptance:**

* Measured on target hardware, sustained throughput is at least 5 FPS.
* This is verified by a hardware-in-the-loop test, which stays out of the
  default suite and out of CI (`REQ-DEV-003`).

---

## Target state machine

### REQ-TRK-001 — Target states

**Statement:** The system SHALL have exactly three target states: `SEARCHING`,
`TARGET_LOCKED` and `TARGET_LOST`. The initial state SHALL be `SEARCHING`.

**Acceptance:**

* A freshly constructed state machine reports `SEARCHING`.
* No transition can produce a state outside this set.

### REQ-TRK-002 — Lock confirmation requires three consecutive detections of one track

**Statement:** While in `SEARCHING`, the system SHALL transition to
`TARGET_LOCKED` only after the **same track** (`REQ-TRK-007`) has been detected
in three consecutive frames.

**Rationale:** Single-frame detections are too noisy to justify firing water.
Requiring the same track, rather than mere presence, prevents three different
birds passing through the frame from confirming a target that was never there,
and gives `REQ-AIM-001` a well-defined position to aim at.

**Acceptance:**

* `FOUND`, `FOUND` on one track → still `SEARCHING`.
* `FOUND`, `FOUND`, `FOUND` on one track → `TARGET_LOCKED`.
* `FOUND`, `FOUND`, `NONE`, `FOUND`, `FOUND` → still `SEARCHING`
  (see `REQ-TRK-003`).
* Three consecutive frames each containing a detection, but too far apart to
  associate into one track, do **not** confirm a target.

### REQ-TRK-007 — Detections are associated into tracks across frames

**Statement:** The system SHALL associate each detection with the nearest
existing track whose centroid lies within a configured association radius. A
detection that matches no track SHALL start a new track. A track that receives
no detection in a frame SHALL have its consecutive-detection count reset
(`REQ-TRK-003`).

**Rationale:** Confirmation and aiming are both statements about *a pigeon*,
not about *a frame*. Without association neither is well defined.

**Acceptance:**

* Two detections in successive frames within the association radius produce one
  track with a count of two.
* Two detections in successive frames beyond the association radius produce two
  tracks, each with a count of one.
* Association is deterministic: identical frame sequences produce identical
  tracks, independent of iteration order (`REQ-DEV-002`).

### REQ-TRK-008 — One target is engaged at a time

**Statement:** When several tracks are confirmable in the same frame, the
system SHALL select exactly one — the detection with the largest bounding-box
area — and SHALL ignore the others for the duration of the engagement. Ties
SHALL be broken deterministically by ascending X then ascending Y centroid
coordinate.

**Rationale:** Pigeons are gregarious, so multi-bird frames are the normal
case, not an edge case. Refusing to act on ambiguous frames would leave the
system permanently idle. The largest detection is the closest, and therefore
the one the deterrent can most plausibly reach.

**Acceptance:**

* A frame containing three confirmable tracks produces exactly one engagement.
* Two detections of identical area produce a stable, documented choice, and the
  same choice on every run.
* No fire command is ever issued for more than one target in one engagement.

### REQ-TRK-003 — Non-detection resets the confirmation counter

**Statement:** While in `SEARCHING`, a frame classified `NONE` SHALL reset the
consecutive-detection counter to zero.

**Acceptance:**

* After any interleaved `NONE`, three further consecutive `FOUND` frames are
  required before the state becomes `TARGET_LOCKED`.

### REQ-TRK-004 — Re-verification before firing

**Statement:** After entering `TARGET_LOCKED` and after aiming commands have
been sent, the system SHALL classify the **immediately following frame** before
issuing a fire command. There SHALL be no wall-clock timeout on this step.

**Rationale:** The target may leave while the servos are moving. Expressing the
deadline in frames rather than milliseconds keeps the target state machine a
pure function of its inputs, with no clock to inject and no timing-sensitive
tests (`REQ-DEV-002`).

**Acceptance:**

* Verification frame `FOUND` → a fire command is issued.
* Verification frame `NONE` → no fire command is issued.
* The state machine reads no clock on this path.
* If no further frame arrives, the engagement remains unresolved and no fire
  command is issued.

### REQ-TRK-005 — Target loss

**Statement:** If the verification frame required by `REQ-TRK-004` is classified
`NONE`, the system SHALL transition to `TARGET_LOST`, SHALL NOT issue a fire
command, and SHALL then return to `SEARCHING` with a zeroed confirmation
counter.

**Acceptance:**

* The sequence `FOUND` ×3 then `NONE` yields states `TARGET_LOCKED` →
  `TARGET_LOST` → `SEARCHING`.
* No fire command is emitted anywhere in that sequence.

### REQ-TRK-006 — `TARGET_LOST` is only reachable from `TARGET_LOCKED`

**Statement:** The system SHALL enter `TARGET_LOST` only from `TARGET_LOCKED`.

**Acceptance:**

* No sequence of frames starting in `SEARCHING` reaches `TARGET_LOST` without
  passing through `TARGET_LOCKED`.

---

## Targeting

### REQ-AIM-001 — Angle calculation

**Statement:** The camera SHALL be mounted on the pan/tilt rig, boresighted
with the deterrent nozzle. For a confirmed target the system SHALL compute the
X and Y servo angles as an angular offset from the image centre, derived from
the detection centroid and the camera's horizontal and vertical fields of view,
corrected by a configured boresight offset.

**Rationale:** A boresighted camera makes the aiming direction independent of
the range to the target, so no depth estimate is required from a single camera.
Calibration reduces to two field-of-view values and a boresight offset.

**Acceptance:**

* A target at the image centre yields the configured neutral angles plus the
  boresight offset.
* A target at the horizontal image edge yields an offset of half the horizontal
  field of view.
* Known fixture positions yield the documented expected angles within a
  documented tolerance.
* The computation uses no range, depth or scale estimate.

### REQ-AIM-002 — Angles are clamped to the mechanical envelope

**Statement:** Computed angles SHALL be clamped to the configured minimum and
maximum for each axis before being sent to the actuator system. The deployment
envelope is X ∈ [−90°, +90°] and Y ∈ [0°, +45°], where X = 0° points straight
ahead, positive X is to the right, **Y = 0° is the horizon** and positive Y is
elevated. The system SHALL NOT command any elevation below the horizon. A
default-constructed configuration SHALL have an empty envelope and SHALL
therefore permit no firing until explicitly configured (`REQ-SAF-004`).

**Rationale:** Anchoring Y = 0° at the horizon makes "never point at the
ground" a property of the datum itself rather than a rule someone has to
remember. A negative elevation is not merely out of range, it is
unrepresentable in the configured envelope.

**Acceptance:**

* An out-of-range computed angle is emitted as the corresponding limit.
* No angle outside the configured envelope is ever emitted.
* A default-constructed configuration emits no fire command for any input.
* A target below the horizon in the image clamps to Y = 0°, never below.
* The nozzle is never commanded below the horizontal.

### REQ-AIM-003 — Calibration is data, loaded outside `core/`

**Statement:** Camera fields of view, boresight offset, neutral angles,
mechanical envelope and association radius SHALL be stored in a
version-controlled key/value configuration file, parsed in `raspberry/` and
passed into `core/` as a plain value type. `core/` SHALL NOT read the file
itself.

**Rationale:** Keeps `core/` free of I/O (`REQ-DEV-001`) and keeps calibration
reviewable in version control rather than living in someone's memory. A
key/value format needs no third-party parser, which matters on a 1 GB target.

**Acceptance:**

* `core/` contains no file access; the configuration arrives as a struct.
* A missing or malformed configuration file is rejected with a clear error and
  leaves the system in the safe, non-firing default state (`REQ-SAF-004`).
* The same configuration file always produces the same in-memory values.

---

## Communication

### REQ-COM-001 — Documented protocol at the device boundary

**Statement:** The Raspberry Pi ↔ Arduino protocol SHALL be specified in
`docs/architecture/` and SHALL be implementable and testable without hardware.

**Acceptance:**

* Encoding and decoding are covered by tests that use no serial device.
* Malformed input is rejected without undefined behaviour.

### REQ-COM-002 — Fail-safe on communication loss

**Statement:** If the actuator link becomes unavailable, the system SHALL
abandon the current engagement, SHALL NOT issue a fire command, and SHALL return
to `SEARCHING`.

**Acceptance:**

* With a simulated link that fails after locking, no fire command is emitted and
  the final state is `SEARCHING`.

---

## Safety

> These requirements exist because the system aims a water jet under autonomous
> control. They are not optional.

### REQ-SAF-001 — Bounded fire duration

**Statement:** A single fire command SHALL activate the water actuator for no
longer than a configured maximum duration, defaulting to **500 ms**, after
which it SHALL deactivate without requiring a further command. The Arduino
SHALL enforce this bound independently, so that the actuator deactivates even
if the Raspberry Pi stops responding mid-engagement.

**Rationale:** A short burst is the deterrent; a long one wastes water and
soaks the surroundings. Enforcement on the Arduino is what makes the bound real
rather than aspirational — a limit that only the commanding device enforces
fails exactly when the commanding device fails.

**Acceptance:**

* A simulated actuator that receives a fire command is inactive again after the
  configured maximum duration.
* A simulated link that goes silent immediately after a fire command still
  results in an inactive actuator.
* A configured duration longer than the firmware's own bound is rejected or
  truncated by the firmware, not obeyed.

### REQ-SAF-005 — Engagement rate is limited

**Statement:** After firing, the system SHALL NOT begin another engagement for
a configured cool-down period, defaulting to **2 seconds**, and SHALL NOT
exceed a configured maximum engagement rate, defaulting to **6 engagements per
minute**.

**Rationale:** Deterrence, not harassment. Rate limiting also protects the pump
from an abusive duty cycle and bounds water consumption.

**Acceptance:**

* A fire command immediately followed by another confirmed target produces no
  second fire command until the cool-down has elapsed.
* A sequence of confirmed targets arriving faster than the configured rate
  produces no more than the configured number of fire commands per minute.
* Rate limiting is driven by an injected monotonic clock, never by the wall
  clock, so the behaviour is deterministic under test (`REQ-DEV-002`).

### REQ-SAF-002 — No fire without a fresh confirmation

**Statement:** The system SHALL NOT issue a fire command unless `REQ-TRK-002`
and `REQ-TRK-004` have both been satisfied for the current engagement.

**Acceptance:**

* Exhaustive state-machine tests show no reachable path to a fire command that
  skips either condition.

### REQ-SAF-003 — Configurable exclusion zone

**Statement:** The system SHALL support a configured region of the angle space
in which firing is prohibited, and SHALL NOT fire while aimed inside it. The
zone SHALL be configurable per installation. The reference installation
configures **no exclusion zone**; the mechanism is nevertheless required and
SHALL remain implemented and tested.

**Rationale:** Prevents spraying neighbours, windows, walkways, or people. The
current installation has nothing within reach that needs excluding, but a
deterrent that cannot be told where not to fire is one that must be rebuilt the
first time it is repositioned. Retaining the mechanism costs a comparison;
retrofitting it later costs a redesign.

**Acceptance:**

* A target that resolves to angles inside a configured exclusion zone produces
  no fire command.
* With no exclusion zone configured, firing is permitted anywhere within the
  mechanical envelope.
* The exclusion-zone test suite runs with an explicitly configured zone, so the
  mechanism stays verified even while the reference installation configures
  none.

### REQ-SAF-004 — Safe state on startup and shutdown

**Statement:** On startup and on shutdown the actuator system SHALL be placed in
a state in which the water actuator is inactive.

**Acceptance:**

* The simulated actuator reports inactive immediately after construction and
  after shutdown.

---

## Development environment

### REQ-DEV-001 — Hardware-free build, run and test

**Statement:** The complete system SHALL build, run in simulation and pass its
test suite on macOS with no Raspberry Pi, Arduino, camera, servo, water actuator
or GPIO present.

**Acceptance:**

* `scripts/check.sh` passes on a clean macOS checkout.

### REQ-DEV-002 — Deterministic tests

**Statement:** The test suite SHALL be deterministic: no wall-clock dependence,
no unseeded randomness, no network access, no reliance on test ordering.

**Acceptance:**

* Repeated runs, including shuffled runs, produce identical results.

### REQ-DEV-003 — Hardware-in-the-loop tests are segregated

**Statement:** Tests requiring physical hardware SHALL be excluded from the
default test suite and from CI.

**Acceptance:**

* The default `ctest` invocation runs no test that opens a serial port or a
  camera device.

---

## Configured parameters

Every tunable value in one place. The requirement is authoritative; this table
is a reference. Defaults are the values the system ships with.

| Parameter                  | Default        | Requirement   |
| -------------------------- | -------------- | ------------- |
| Confirmation frames        | 3              | `REQ-TRK-002` |
| Track association radius   | *uncalibrated* | `REQ-TRK-007` |
| Verification deadline      | next frame     | `REQ-TRK-004` |
| Detection throughput       | 5 FPS          | `REQ-DET-003` |
| Camera horizontal FOV      | *uncalibrated* | `REQ-AIM-001` |
| Camera vertical FOV        | *uncalibrated* | `REQ-AIM-001` |
| Boresight offset           | *uncalibrated* | `REQ-AIM-001` |
| X angle envelope           | −90° … +90°    | `REQ-AIM-002` |
| Y angle envelope           | 0° … +45°      | `REQ-AIM-002` |
| Default envelope           | empty          | `REQ-AIM-002` |
| Maximum fire duration      | 500 ms         | `REQ-SAF-001` |
| Cool-down after firing     | 2 s            | `REQ-SAF-005` |
| Maximum engagement rate    | 6 / minute     | `REQ-SAF-005` |
| Exclusion zone             | none configured | `REQ-SAF-003` |

*uncalibrated* means the value is a property of the physical rig and is
recorded during bring-up. Until then the empty default envelope prevents
firing, so an uncalibrated system is inert rather than dangerous.

The reference installation configures no exclusion zone (Q9). The mechanism is
still required and still tested, because the zone is a property of where the
rig is mounted, and rigs get moved.

## Open questions

Unresolved specification gaps. **Agents must not invent answers to these** —
raise them with the maintainer.

*None outstanding.* Every question raised so far has been resolved; see below.
Add new rows here rather than guessing.

| #  | Question | Blocks |
| -- | -------- | ------ |
| —  | —        | —      |

### Resolved

| #  | Question                                          | Resolution                                                                 |
| -- | ------------------------------------------------- | -------------------------------------------------------------------------- |
| Q1 | Multiple simultaneous pigeons                     | Engage the largest detection, deterministic tie-break. `REQ-TRK-008`, ADR-0003 |
| Q2 | Frame rate and verification timeout               | 5 FPS; verification uses the next frame, no clock. `REQ-DET-003`, `REQ-TRK-004`, ADR-0005 |
| Q3 | Coordinate frames and calibration                 | Camera boresighted on the rig; key/value config parsed outside `core/`. `REQ-AIM-001`, `REQ-AIM-003`, ADR-0004 |
| Q4 | Mechanical angle limits                           | X ∈ [−90°, +90°], Y ∈ [0°, +45°]; empty by default. `REQ-AIM-002`            |
| Q5 | Maximum fire duration and cool-down               | 500 ms burst, 2 s cool-down, enforced on the Arduino too. `REQ-SAF-001`, `REQ-SAF-005` |
| Q6 | Maximum engagement rate                           | 6 engagements per minute. `REQ-SAF-005`                                     |
| Q7 | Spatial association across frames                 | Associate detections into tracks; confirm one track. `REQ-TRK-007`, ADR-0003 |
| Q8 | Y-axis datum                                      | Y = 0° is the horizon; elevation is never negative. `REQ-AIM-002`            |
| Q9 | Exclusion zone for this installation              | None configured. The mechanism is retained and tested. `REQ-SAF-003`        |
