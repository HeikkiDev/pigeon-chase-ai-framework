---
applyTo: "raspberry/**/*.cpp,raspberry/**/*.hpp,raspberry/**/*.h"
---

# Raspberry Pi integration layer

> **This layer is a future phase.** Nothing here may become a prerequisite for
> building, running or testing the system on macOS (`REQ-DEV-001`).

## Scope

`raspberry/` adapts real hardware to interfaces defined in `core/`:

- Camera capture → the frame source interface.
- Object detection model runtime → the detector interface.
- Serial port handling → the actuator link interface.
- Process startup, configuration loading and logging.

It contains **adapters only**. Detection confirmation, the target state
machine, targeting maths and safety policy live in `core/` and must not be
duplicated or re-implemented here.

## Dependency direction

`raspberry/` depends on `core/`. `core/` never depends on `raspberry/`.

## Resource constraints

The target is a Raspberry Pi 3 Model B: 4 cores, **1 GB RAM**, no GPU
acceleration for inference.

- Every allocation of frame-sized data matters. Reuse buffers; avoid copies.
- Choose quantised, small object-detection models. Justify the choice in an ADR
  including its measured resident memory.
- Avoid unbounded queues. A backlog of frames will exhaust memory.
- Prefer streaming over batching.
- Do not assume swap is available or fast.

## Hardware interaction

- Every hardware resource is acquired with RAII and released deterministically.
- Every hardware call can fail. Handle it; never ignore a return code.
- On failure of the actuator link, abandon the engagement and return to
  `SEARCHING` (`REQ-COM-002`).
- On startup and shutdown, command the actuator system into its safe state
  (`REQ-SAF-004`).
- Never block the control loop indefinitely on a device read.

## Testing

- Everything here must have a simulated counterpart so the pipeline runs on
  macOS.
- Tests that touch a real camera or serial port are hardware-in-the-loop tests:
  keep them out of the default suite and out of CI (`REQ-DEV-003`).
- Adapter logic that does not need a device (parsing, buffering, protocol
  framing) must still be unit tested.
