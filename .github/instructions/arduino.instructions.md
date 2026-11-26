---
applyTo: "arduino/**/*.cpp,arduino/**/*.h,arduino/**/*.ino"
---

# Arduino firmware

> **This layer is a future phase.** Nothing here may become a prerequisite for
> building, running or testing the system on macOS (`REQ-DEV-001`).

The Arduino is the last thing between software and a jet of water. Treat it as
safety-critical firmware, not as a sketch.

## Scope

- Receive commands from the Raspberry Pi over USB serial.
- Drive the X and Y servos.
- Control the water actuator.
- Enforce hardware safety limits **independently** of the Raspberry Pi.

## Safety rules

These are not negotiable.

- **Enforce limits locally.** Check every requested angle against the
  mechanical envelope in firmware and **refuse** one outside it: reply
  `REJECTED_ANGLE` and leave the servos where they are, do not clamp and move
  (`REQ-AIM-002`, Q26). Never trust the host to have clamped it.
- **Bound the fire duration.** The water actuator self-deactivates after the
  configured maximum, without needing a stop command (`REQ-SAF-001`).
- **Fail safe.** On reset, on malformed input and on watchdog expiry,
  deactivate the water actuator and hold position (`REQ-SAF-004`,
  `REQ-COM-002`). Loss of the serial link needs no detector of its own: every
  burst ends from the firmware's own timer, so the valve is already closed
  whenever the link goes quiet (`REQ-SAF-001`, ADR-0021 D4).
- **Deactivate on startup**, before anything else, and before enabling servo
  output.
- **Validate every command** before acting: length, checksum, range. A
  malformed command is discarded, never partially executed.
- Use the microcontroller's **hardware** watchdog. A hung firmware must not
  leave the water on, and a software timer cannot be trusted to catch the
  firmware that is itself hung (ADR-0021 D4).

## Embedded constraints

- No dynamic allocation. No `new`, no `malloc`, no `String`, no STL containers
  that allocate. Use fixed-size buffers.
- No exceptions, no RTTI.
- Keep the main loop non-blocking. Never `delay()` inside command handling;
  use explicit timers and a state machine.
- ISRs must be short. Shared variables touched by an ISR are `volatile`, and
  multi-byte accesses are guarded.
- Prefer integer arithmetic; avoid floating point in the hot path.
- Be explicit about integer widths (`uint8_t`, `int16_t`) — `int` is 16-bit on
  many AVR boards.

## Protocol

- The wire protocol is a documented system boundary; see
  `docs/architecture/` (`REQ-COM-001`).
- Parsing and framing logic should be written so it can be compiled and unit
  tested on the host as well as on the device. Keep it free of `Arduino.h`.
  The codec already exists as the shared `protocol/` module (ADR-0021): use it,
  do not reimplement it, so that the two ends cannot disagree about a byte.

## Testing

- Host-testable logic (parsing, clamping, timing state machines) must have
  tests that run in the normal suite on macOS.
- Anything requiring the physical board is hardware-in-the-loop: separate from
  the default suite and from CI (`REQ-DEV-003`).
