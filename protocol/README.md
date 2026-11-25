# protocol/

The Raspberry Pi ↔ Arduino wire protocol: packet format, message encoding and
decoding (`REQ-COM-001`). Compiled by both the host (`raspberry/`) and the
firmware (`arduino/`), so the two devices share one implementation of the
format.

**Status:** implemented (`include/pigeon/protocol/protocol.hpp`,
`src/protocol.cpp`) and tested in `tests/protocol/` (`REQ-COM-001`). Not yet
compiled with avr-gcc. The design is in
[ADR-0020](../docs/decisions/0020-wire-protocol-in-a-shared-protocol-module.md)
(Accepted). The full wire specification is
[`docs/architecture/protocol.md`](../docs/architecture/protocol.md).

## Rules for code in this folder

Stricter than `core/`, because the Arduino compiles it too (ADR-0020):

* Depends on nothing: no `pigeon/core` include, and no header beyond
  `<stdint.h>` and `<stddef.h>`. The Uno's compiler (avr-gcc) has no C++
  standard library, so `<cstdint>` and `<cstddef>` do not exist there.
* Fixed-width integers only. No floating point.
* No allocation, no exceptions, no RTTI, no virtual functions.
* Buffers are owned by the caller and have fixed sizes.
* No clock and no randomness (`REQ-DEV-002`).
* No hardware or platform headers (`REQ-DEV-001`).
