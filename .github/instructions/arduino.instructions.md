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

- **Enforce limits locally.** Clamp every requested angle to the mechanical
  envelope in firmware. Never trust the host to have clamped it
  (`REQ-AIM-002`).
- **Bound the fire duration.** The water actuator self-deactivates after the
  configured maximum, without needing a stop command (`REQ-SAF-001`).
- **Fail safe.** On reset, on malformed input, on watchdog expiry, and on loss
  of the serial link, deactivate the water actuator and hold position
  (`REQ-SAF-004`, `REQ-COM-002`).
- **Deactivate on startup**, before anything else, and before enabling servo
  output.
- **Validate every command** before acting: length, checksum, range. A
  malformed command is discarded, never partially executed.
- Use a watchdog. A hung firmware must not leave the water on.

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

## Testing

- Host-testable logic (parsing, clamping, timing state machines) must have
  tests that run in the normal suite on macOS.
- Anything requiring the physical board is hardware-in-the-loop: separate from
  the default suite and from CI (`REQ-DEV-003`).
