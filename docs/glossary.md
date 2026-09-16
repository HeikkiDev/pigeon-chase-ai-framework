# Glossary

Shared vocabulary. Use these terms exactly; do not introduce synonyms.
Ambiguous terminology is one of the main causes of agent drift.

| Term                   | Definition                                                                                                     |
| ---------------------- | -------------------------------------------------------------------------------------------------------------- |
| **Frame**              | A single image supplied to the detection component.                                                             |
| **Frame view**         | The borrowed, non-owning representation of a frame inside `core/`: dimensions, stride and packed RGB888 bytes, owned by the producer (`REQ-DET-002`, `REQ-DET-004`). |
| **Detection result**   | The classification of one frame: `NONE` or `FOUND`. Nothing else (`REQ-DET-001`).                               |
| **Detection outcome**  | A detection result together with the detections that justify it. `FOUND` if and only if at least one detection is present. |
| **Detection**          | One pigeon located within a single frame, with a centroid and a bounding box.                                   |
| **Track**              | A sequence of detections in successive frames associated as the same pigeon (`REQ-TRK-007`). Discarded as soon as a frame brings it no detection (`REQ-TRK-009`, ADR-0010). |
| **Retirement**         | The discarding of the engaged track when its engagement ends, by burst or by abandonment. It resets the confirmation counter, so the same bird must earn three fresh consecutive detections before it can be confirmed again (`REQ-TRK-012`, ADR-0013). |
| **Track identifier**   | The opaque identity of a track. Assigned in ascending order, never reused within a run, never interpreted as an index or a priority. |
| **Association radius** | The maximum centroid distance at which a detection is matched to an existing track. Inclusive: a centroid exactly the radius away associates (`REQ-TRK-007`). |
| **Confirmation counter** | The count of consecutive frames in which one track was detected. It resets when the track is absent (`REQ-TRK-003`) and when its engagement ends (`REQ-TRK-012`); both resets happen by the track being discarded, so a live track's count is never zero (`REQ-TRK-009`, ADR-0013). |
| **Confirmed target**   | A track detected in three consecutive frames (`REQ-TRK-002`).                                                   |
| **Selected target**    | The one confirmed track engaged in this engagement — the largest by bounding-box area (`REQ-TRK-008`).          |
| **`SEARCHING`**        | Target state: no confirmed target. The initial state.                                                           |
| **`TARGET_LOCKED`**    | Target state: a confirmed target exists and is being engaged.                                                   |
| **`TARGET_LOST`**      | Target state: the verification frame after locking was `NONE`. Reachable only from `TARGET_LOCKED`.             |
| **Engagement**         | One complete cycle from lock to fire or to loss. It ends when the fire command is issued; the machine returns to `SEARCHING`, the engaged track is retired, and the same bird may be engaged again once it has earned three fresh detections and the cool-down has expired (`REQ-TRK-011`, `REQ-TRK-012`, ADR-0012, ADR-0013). |
| **Engagement intent**  | What the state machine asks for after a frame — keep searching, aim, fire or abandon. An intent is not a command: it must still be authorised (ADR-0007). |
| **Verification frame** | The extra frame examined after aiming and before firing. It satisfies verification only if it re-detects the locked track itself (`REQ-TRK-004`, `REQ-TRK-010`, ADR-0009). |
| **Aiming command**     | A message instructing the actuator system to move to given X and Y angles.                                      |
| **Fire command**       | A message instructing the actuator system to activate the water actuator.                                       |
| **Safe-state command** | A message instructing the actuator system to deactivate the water actuator. Sent on startup, on shutdown and when an engagement is abandoned (`REQ-SAF-004`). |
| **Fire authorisation** | The safety policy's verdict on an intent to fire: granted with a clamped aim and a bounded duration, or refused with a named reason (`REQ-SAF-002`). |
| **Refusal reason**     | The named cause of a refused burst. Each `LinkStatus` other than `OK` has its own, shared with no other status, so a refusal is diagnosable (`REQ-SAF-008`, ADR-0015). |
| **Rate window**        | The one-minute span against which the six-burst limit is counted. Half-open: a burst exactly one minute old has left it (`REQ-SAF-005`, ADR-0014). |
| **Actuator link**      | The seam between `core/` and the actuator system. Failure is a returned status, never an exception (`REQ-COM-002`). |
| **Monotonic clock**    | The injected, never-decreasing time source. The only way `core/` learns the time, and used only for the cool-down and rate limit (ADR-0005). |
| **Servo frame**        | The angle convention: X = 0° straight ahead with positive to the right, Y = 0° at the horizon with positive elevated (`REQ-AIM-002`, ADR-0004). |
| **Image frame**        | The pixel convention: origin top-left, +x right, +y **down**. The vertical sign flips between this and the servo frame. |
| **Deterrent**          | The aimed water jet assembly. Preferred over "water gun" in specifications.                                     |
| **Actuator system**    | The Arduino side: servos plus water actuator. Simulated during development.                                     |
| **Exclusion zone**     | The configured axis-aligned rectangle of servo-angle space in which firing is prohibited. An installation configures at most one (`REQ-SAF-003`, `REQ-SAF-006`). |
| **Safety policy**      | The component that turns an intent to fire into a fire authorisation, applying the envelope, the exclusion zone, the cool-down and the rate limit. Any command it lets through and the caller transmits counts against the limits (`REQ-SAF-001`…`REQ-SAF-007`). |
| **Mechanical envelope** | The configured minimum and maximum angle of each servo axis (`REQ-AIM-002`). Empty by default, so an uncalibrated system cannot fire. |
| **Boresight**          | The alignment of the camera's optical axis with the nozzle's axis. The camera rides the pan/tilt rig (`REQ-AIM-001`, ADR-0004). |
| **Boresight offset**   | The configured angular correction for imperfect camera/nozzle alignment (`REQ-AIM-001`).                        |
| **Cool-down**          | The minimum interval after a transmitted burst before another may be sent. It has elapsed once `elapsed >= cool_down`, so a burst at exactly that instant is permitted (`REQ-SAF-005`, ADR-0014). |
| **Simulated component** | A production-quality software implementation of a hardware interface, used for development and CI.             |
| **Mock**               | A test double asserting on interactions. Used inside tests only, never shipped.                                 |
| **Fixture**            | A recorded, version-controlled input (image or scenario) used for deterministic tests.                          |
| **Scenario**           | An ordered sequence of frames plus expected outcomes, driven through the full pipeline.                         |
| **The gate**           | `scripts/check.sh` — the single pass/fail health check for the repository.                                      |
| **Traceability**       | The mapping from `REQ-*` IDs to the tests that verify them, produced by `scripts/trace.sh`.                     |
