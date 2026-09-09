---
applyTo: "core/**/*.cpp,core/**/*.hpp"
---

# `core/` — hardware-independent domain logic

`core/` is the heart of the system and the reason the whole project is
testable. It must remain buildable and runnable on any machine with a C++20
compiler and nothing else.

## Absolute rules

- `core/` depends on the **C++ standard library only**. Not on `raspberry/`,
  not on `arduino/`, not on any third-party library without an ADR.
- **No hardware headers.** No camera, GPIO, `wiringPi`, `pigpio`, `Arduino.h`,
  `termios`, serial, OS or platform headers. A single such include is a
  blocking architectural defect (`REQ-DET-002`, `REQ-DEV-001`).
- **No I/O.** No file access, no sockets, no `std::cout` logging buried in
  domain logic. Return data; let the caller decide what to do with it.
- **No wall clock, no randomness.** Inject a clock or a generator through an
  interface so tests stay deterministic (`REQ-DEV-002`).
- **No global mutable state** and no singletons.

## What belongs here

- Detection contracts (the `NONE` / `FOUND` result type and its interface).
- The target state machine (`SEARCHING`, `TARGET_LOCKED`, `TARGET_LOST`).
- Targeting maths: image position to servo angles, and clamping.
- Command generation for the actuator system, and protocol encoding/decoding.
- Safety policy: exclusion zones, bounded fire duration, fail-safe defaults.
- Interfaces for everything hardware-shaped, plus their simulated
  implementations.

## What does not belong here

- Camera capture, model runtime loading, serial port handling → `raspberry/`.
- Servo pulses, actuator pin control → `arduino/`.

## Style emphasis

- Prefer pure functions. The state machine in particular should be a pure
  function of its current state and the incoming detection result; this is what
  makes exhaustive safety testing possible (`REQ-SAF-002`).
- Make illegal states unrepresentable. Use `enum class` and strong types rather
  than raw `int` or `bool` flags for states, angles and results.
- Give physical quantities explicit units and coordinate frames in their name
  or their documentation. Ambiguous angles are a safety hazard.
- Default to the safe value: a default-constructed configuration must not
  permit firing (`REQ-SAF-004`).
