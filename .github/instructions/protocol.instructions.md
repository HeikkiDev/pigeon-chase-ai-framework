---
applyTo: "protocol/**/*.cpp,protocol/**/*.hpp"
---

# `protocol/` — the device wire codec

The one implementation of the Raspberry Pi ↔ Arduino wire encoding, compiled
into the host *and* into the firmware (ADR-0021 D3). The encoding is specified
in `docs/architecture/device-protocol.md`, whose test vectors are normative:
this module's tests reproduce them byte for byte.

## Absolute rules

- **A freestanding subset of C++.** Fixed-width integers from `<cstdint>`,
  `<cstddef>`, and nothing else from the standard library that allocates or
  needs a runtime: no `<vector>`, `<string>`, `<memory>`, `<map>`, `<optional>`,
  `<variant>`, `<span>`, no streams. The firmware toolchain is not chosen and
  may be older and smaller than the host's.
- **No allocation, no exceptions, no RTTI, no virtual functions.** Callers
  supply every buffer. Failure is a returned status, never a throw.
- **C++17 constructs only.** No concepts, no `<=>`, no `std::span`, no
  designated initialisers. The repository builds it as C++20 like everything
  else; the constraint is what keeps a common Arduino toolchain able to build it.
- **No hardware, platform, I/O or clock.** Same as `core/` (`REQ-DEV-001`,
  `REQ-DEV-002`): no `Arduino.h`, no `termios.h`, no wall clock, no randomness.
  Time, ports and timers belong to `raspberry/` and `arduino/`.
- **No dependency on `core/`, `raspberry/` or `arduino/`.** Dependencies point
  into this module, never out of it.
- **Total on every input.** Every function accepts any bytes whatsoever and
  rejects what is malformed with a status. No input may cause undefined
  behaviour, an out-of-range read or write, or an unbounded loop
  (`REQ-COM-001`). Bounds are checked on every access.
- **Integer widths are explicit.** `int` is 16-bit on many AVR boards; never
  rely on its size, and mind integer promotion in arithmetic on `uint8_t`.

## Style

- Header-only. `src/public_headers.cpp` exists only so the compiler and
  clang-tidy see every header (as in `core/`).
- Names follow `.clang-tidy`. Everything is `noexcept`.
- A message type, result code or timing value written here must match the
  specification; if they disagree, the specification is the authority and the
  disagreement is a defect to raise, not to resolve in code.

## Testing

- Every behaviour is tested against the specification's vectors, including the
  inputs a receiver must reject (`N1`–`N9`).
- Tests need no serial port or device (`REQ-DEV-003`). They are deterministic
  (`REQ-DEV-002`): fixed byte arrays, no randomness unless explicitly seeded.
