# Glossary

Shared vocabulary. Use these terms exactly; do not introduce synonyms.
Ambiguous terminology is one of the main causes of agent drift.

| Term                   | Definition                                                                                                     |
| ---------------------- | -------------------------------------------------------------------------------------------------------------- |
| **Frame**              | A single image supplied to the detection component.                                                             |
| **Detection result**   | The classification of one frame: `NONE` or `FOUND`. Nothing else (`REQ-DET-001`).                               |
| **Confirmation counter** | The count of consecutive `FOUND` frames while in `SEARCHING`. Reset to zero by a `NONE` frame (`REQ-TRK-003`). |
| **Confirmed target**   | A target that has satisfied the three-consecutive-`FOUND` rule (`REQ-TRK-002`).                                 |
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
| **Mechanical envelope** | The configured minimum and maximum angle of each servo axis (`REQ-AIM-002`).                                    |
| **Simulated component** | A production-quality software implementation of a hardware interface, used for development and CI.             |
| **Mock**               | A test double asserting on interactions. Used inside tests only, never shipped.                                 |
| **Fixture**            | A recorded, version-controlled input (image or scenario) used for deterministic tests.                          |
| **Scenario**           | An ordered sequence of frames plus expected outcomes, driven through the full pipeline.                         |
| **The gate**           | `scripts/check.sh` — the single pass/fail health check for the repository.                                      |
| **Traceability**       | The mapping from `REQ-*` IDs to the tests that verify them, produced by `scripts/trace.sh`.                     |
