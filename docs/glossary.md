# Glossary

Shared vocabulary. Use these terms exactly; do not introduce synonyms.
Ambiguous terminology is one of the main causes of agent drift.

| Term                   | Definition                                                                                                     |
| ---------------------- | -------------------------------------------------------------------------------------------------------------- |
| **Frame**              | A single image supplied to the detection component.                                                             |
| **Detection result**   | The classification of one frame: `NONE` or `FOUND`. Nothing else (`REQ-DET-001`).                               |
| **Detection**          | One pigeon located within a single frame, with a centroid and a bounding box.                                   |
| **Track**              | A sequence of detections in successive frames associated as the same pigeon (`REQ-TRK-007`).                    |
| **Association radius** | The maximum centroid distance at which a detection is matched to an existing track (`REQ-TRK-007`).             |
| **Confirmation counter** | The count of consecutive frames in which one track was detected. Reset to zero when that track is absent (`REQ-TRK-003`). |
| **Confirmed target**   | A track detected in three consecutive frames (`REQ-TRK-002`).                                                   |
| **Selected target**    | The one confirmed track engaged in this engagement — the largest by bounding-box area (`REQ-TRK-008`).          |
| **`SEARCHING`**        | Target state: no confirmed target. The initial state.                                                           |
| **`TARGET_LOCKED`**    | Target state: a confirmed target exists and is being engaged.                                                   |
| **`TARGET_LOST`**      | Target state: the verification frame after locking was `NONE`. Reachable only from `TARGET_LOCKED`.             |
| **Engagement**         | One complete cycle from lock to fire or to loss.                                                                |
| **Verification frame** | The extra frame classified after aiming and before firing (`REQ-TRK-004`).                                      |
| **Aiming command**     | A message instructing the actuator system to move to given X and Y angles.                                      |
| **Fire command**       | A message instructing the actuator system to activate the water actuator.                                       |
| **Deterrent**          | The aimed water jet assembly. Preferred over "water gun" in specifications.                                     |
| **Actuator system**    | The Arduino side: servos plus water actuator. Simulated during development.                                     |
| **Exclusion zone**     | A configured region of angle space in which firing is prohibited (`REQ-SAF-003`).                                |
| **Mechanical envelope** | The configured minimum and maximum angle of each servo axis (`REQ-AIM-002`). Empty by default, so an uncalibrated system cannot fire. |
| **Boresight**          | The alignment of the camera's optical axis with the nozzle's axis. The camera rides the pan/tilt rig (`REQ-AIM-001`, ADR-0004). |
| **Boresight offset**   | The configured angular correction for imperfect camera/nozzle alignment (`REQ-AIM-001`).                        |
| **Cool-down**          | The minimum interval after firing before another engagement may begin (`REQ-SAF-005`).                          |
| **Simulated component** | A production-quality software implementation of a hardware interface, used for development and CI.             |
| **Mock**               | A test double asserting on interactions. Used inside tests only, never shipped.                                 |
| **Fixture**            | A recorded, version-controlled input (image or scenario) used for deterministic tests.                          |
| **Scenario**           | An ordered sequence of frames plus expected outcomes, driven through the full pipeline.                         |
| **The gate**           | `scripts/check.sh` — the single pass/fail health check for the repository.                                      |
| **Traceability**       | The mapping from `REQ-*` IDs to the tests that verify them, produced by `scripts/trace.sh`.                     |
