# System Requirements

This document is the **authoritative specification** of what the system must do.

Every requirement has a stable ID. Implementation and tests reference these IDs,
and `scripts/trace.sh` verifies that every requirement marked `Implemented` is
covered by at least one test.

## How to read this document

Each requirement has:

| Field          | Meaning                                                         |
| -------------- | --------------------------------------------------------------- |
| **ID**         | Stable identifier, e.g. `REQ-DET-001`. Never reuse or renumber.  |
| **Status**     | `Draft`, `Approved`, `Implemented`, or `Superseded`.             |
| **Statement**  | Normative behaviour, using SHALL / SHALL NOT.                    |
| **Rationale**  | Why the requirement exists.                                      |
| **Acceptance** | Objectively checkable criteria a test can assert.                |

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
* Never delete a requirement; mark it `Superseded` and link the replacement.

---

## Detection

### REQ-DET-001 — Frame classification

**Status:** Draft

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

**Status:** Draft

**Statement:** The detection component SHALL accept frames through an interface
and SHALL NOT depend on any camera, GPIO or platform-specific API.

**Rationale:** Enables deterministic testing from recorded fixtures
(`REQ-DEV-002`).

**Acceptance:**

* `core/` contains no include of a camera, GPIO, Raspberry Pi or Arduino header.
* The full detection path can be exercised from files on disk.

---

## Target state machine

### REQ-TRK-001 — Target states

**Status:** Draft

**Statement:** The system SHALL have exactly three target states: `SEARCHING`,
`TARGET_LOCKED` and `TARGET_LOST`. The initial state SHALL be `SEARCHING`.

**Acceptance:**

* A freshly constructed state machine reports `SEARCHING`.
* No transition can produce a state outside this set.

### REQ-TRK-002 — Lock confirmation requires three consecutive detections

**Status:** Draft

**Statement:** While in `SEARCHING`, the system SHALL transition to
`TARGET_LOCKED` only after three consecutive frames classified `FOUND`.

**Rationale:** Single-frame detections are too noisy to justify firing water.

**Acceptance:**

* `FOUND`, `FOUND` → still `SEARCHING`.
* `FOUND`, `FOUND`, `FOUND` → `TARGET_LOCKED`.
* `FOUND`, `FOUND`, `NONE`, `FOUND`, `FOUND` → still `SEARCHING`
  (see `REQ-TRK-003`).

### REQ-TRK-003 — Non-detection resets the confirmation counter

**Status:** Draft

**Statement:** While in `SEARCHING`, a frame classified `NONE` SHALL reset the
consecutive-detection counter to zero.

**Acceptance:**

* After any interleaved `NONE`, three further consecutive `FOUND` frames are
  required before the state becomes `TARGET_LOCKED`.

### REQ-TRK-004 — Re-verification before firing

**Status:** Draft

**Statement:** After entering `TARGET_LOCKED` and after aiming commands have
been sent, the system SHALL acquire and classify one additional frame before
issuing a fire command.

**Rationale:** The target may leave while the servos are moving.

**Acceptance:**

* Verification frame `FOUND` → a fire command is issued.
* Verification frame `NONE` → no fire command is issued.

### REQ-TRK-005 — Target loss

**Status:** Draft

**Statement:** If the verification frame required by `REQ-TRK-004` is classified
`NONE`, the system SHALL transition to `TARGET_LOST`, SHALL NOT issue a fire
command, and SHALL then return to `SEARCHING` with a zeroed confirmation
counter.

**Acceptance:**

* The sequence `FOUND` ×3 then `NONE` yields states `TARGET_LOCKED` →
  `TARGET_LOST` → `SEARCHING`.
* No fire command is emitted anywhere in that sequence.

### REQ-TRK-006 — `TARGET_LOST` is only reachable from `TARGET_LOCKED`

**Status:** Draft

**Statement:** The system SHALL enter `TARGET_LOST` only from `TARGET_LOCKED`.

**Acceptance:**

* No sequence of frames starting in `SEARCHING` reaches `TARGET_LOST` without
  passing through `TARGET_LOCKED`.

---

## Targeting

### REQ-AIM-001 — Angle calculation

**Status:** Draft

**Statement:** For a confirmed target the system SHALL compute an X and a Y
servo angle that points the deterrent at the target, using an explicitly
documented camera-to-actuator transform.

**Acceptance:**

* A target at the image centre yields the documented neutral angles.
* Known fixture positions yield the documented expected angles within a
  documented tolerance.

### REQ-AIM-002 — Angles are clamped to the mechanical envelope

**Status:** Draft

**Statement:** Computed angles SHALL be clamped to the configured minimum and
maximum for each axis before being sent to the actuator system.

**Acceptance:**

* An out-of-range computed angle is emitted as the corresponding limit.
* No angle outside the configured envelope is ever emitted.

---

## Communication

### REQ-COM-001 — Documented protocol at the device boundary

**Status:** Draft

**Statement:** The Raspberry Pi ↔ Arduino protocol SHALL be specified in
`docs/architecture/` and SHALL be implementable and testable without hardware.

**Acceptance:**

* Encoding and decoding are covered by tests that use no serial device.
* Malformed input is rejected without undefined behaviour.

### REQ-COM-002 — Fail-safe on communication loss

**Status:** Draft

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

**Status:** Draft

**Statement:** A single fire command SHALL activate the water actuator for no
longer than a configured maximum duration, after which it SHALL deactivate
without requiring a further command.

**Acceptance:**

* A simulated actuator that receives a fire command is inactive again after the
  configured maximum duration.

### REQ-SAF-002 — No fire without a fresh confirmation

**Status:** Draft

**Statement:** The system SHALL NOT issue a fire command unless `REQ-TRK-002`
and `REQ-TRK-004` have both been satisfied for the current engagement.

**Acceptance:**

* Exhaustive state-machine tests show no reachable path to a fire command that
  skips either condition.

### REQ-SAF-003 — Configurable exclusion zone

**Status:** Draft

**Statement:** The system SHALL support a configured region of the angle space
in which firing is prohibited, and SHALL NOT fire while aimed inside it.

**Rationale:** Prevents spraying neighbours, windows, walkways, or people.

**Acceptance:**

* A target that resolves to angles inside the exclusion zone produces no fire
  command.

### REQ-SAF-004 — Safe state on startup and shutdown

**Status:** Draft

**Statement:** On startup and on shutdown the actuator system SHALL be placed in
a state in which the water actuator is inactive.

**Acceptance:**

* The simulated actuator reports inactive immediately after construction and
  after shutdown.

---

## Development environment

### REQ-DEV-001 — Hardware-free build, run and test

**Status:** Draft

**Statement:** The complete system SHALL build, run in simulation and pass its
test suite on macOS with no Raspberry Pi, Arduino, camera, servo, water actuator
or GPIO present.

**Acceptance:**

* `scripts/check.sh` passes on a clean macOS checkout.

### REQ-DEV-002 — Deterministic tests

**Status:** Draft

**Statement:** The test suite SHALL be deterministic: no wall-clock dependence,
no unseeded randomness, no network access, no reliance on test ordering.

**Acceptance:**

* Repeated runs, including shuffled runs, produce identical results.

### REQ-DEV-003 — Hardware-in-the-loop tests are segregated

**Status:** Draft

**Statement:** Tests requiring physical hardware SHALL be excluded from the
default test suite and from CI.

**Acceptance:**

* The default `ctest` invocation runs no test that opens a serial port or a
  camera device.

---

## Open questions

Unresolved specification gaps. **Agents must not invent answers to these** —
raise them with the maintainer.

| #  | Question                                                                                                                       | Blocks                       |
| -- | ------------------------------------------------------------------------------------------------------------------------------ | ---------------------------- |
| Q1 | How are multiple simultaneous pigeons handled — engage one, or ignore the frame?                                               | `REQ-TRK-002`, `REQ-AIM-001` |
| Q2 | What is the target frame rate, and is there a timeout on the `REQ-TRK-004` verification frame?                                 | `REQ-TRK-004`                |
| Q3 | What are the camera and actuator coordinate frames, and how is calibration performed and stored?                               | `REQ-AIM-001`                |
| Q4 | What are the mechanical angle limits of each axis?                                                                             | `REQ-AIM-002`                |
| Q5 | What is the maximum fire duration, and is there a cool-down between engagements?                                               | `REQ-SAF-001`                |
| Q6 | Is there a maximum engagement rate, to avoid harassing the same bird continuously?                                             | —                            |
| Q7 | Should detections be spatially associated across frames (true tracking), or is per-frame presence sufficient for confirmation? | `REQ-TRK-002`                |
