# ADR-0001 — Simulation-first development with hardware behind interfaces

**Status:** Accepted
**Date:** 2026-09-09
**Affects:** `REQ-DEV-001`, `REQ-DEV-002`, `REQ-DEV-003`, `REQ-DET-002`, all modules

## Context

The eventual deployment target is a Raspberry Pi 3B plus an Arduino, with a
camera, two servos and a water actuator. None of that hardware is available
during day-to-day development, and requiring it would make the feedback loop
slow, non-deterministic and impossible to run in CI.

The project is also an experiment in AI-driven engineering. Agents can only be
trusted if they can obtain fast, deterministic, mechanical feedback on every
change. Hardware in the loop destroys that property.

## Decision

All hardware interaction is placed behind interfaces defined in `core/`.
`core/` depends on the C++ standard library only. Every hardware interface has
a simulated implementation that is treated as production code, is version
controlled, and is used by the default test suite.

The complete pipeline — capture, detection, confirmation, targeting,
communication, actuation — runs end to end on macOS with no hardware present.

## Alternatives considered

| Option                                         | Why it was rejected                                                                  |
| ---------------------------------------------- | ------------------------------------------------------------------------------------ |
| Develop directly against the real hardware     | Slow, non-deterministic, not runnable in CI, blocks work whenever hardware is absent. |
| Compile-time `#ifdef` switching for hardware   | Untested code paths, combinatorial build matrix, hides interface drift.                |
| Mocks only, no simulated implementations       | Verifies interactions but never exercises realistic end-to-end behaviour.              |

## Consequences

### Positive

* `scripts/check.sh` gives a complete pass/fail signal on any MacBook.
* Tests are deterministic and therefore trustworthy as agent feedback.
* Hardware can be introduced later without touching core logic.
* Interface boundaries are forced to be explicit and documented.

### Negative / accepted trade-offs

* Simulated implementations are extra code to write and maintain.
* Simulation fidelity gaps will surface only during hardware bring-up; a
  separate hardware-in-the-loop suite will be needed (`REQ-DEV-003`).
* Some indirection exists that a hardware-only design would not need.

### Follow-up work

* Specify the Raspberry Pi ↔ Arduino protocol (`REQ-COM-001`).
* Define the fixture and scenario formats for deterministic tests.
* Define the camera-to-actuator transform and calibration storage
  (Open question Q3).

## Verification

* `scripts/check.sh` configures, builds and tests with no hardware present; CI
  runs it on a clean `macos-latest` runner.
* `.github/instructions/core.instructions.md` forbids hardware headers in
  `core/`, and the code reviewer agent checks this on every change.
* `-Werror` plus clang-tidy prevent the boundary from eroding silently.
