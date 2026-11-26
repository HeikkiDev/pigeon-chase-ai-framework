# ADR-0021 — Device protocol: framed binary messages, heartbeat-based health, a shared codec

**Status:** Accepted
**Date:** 2026-11-26
**Affects:** `REQ-COM-001`, `REQ-COM-002`, `REQ-COM-003`; `REQ-AIM-002`,
`REQ-SAF-001`, `REQ-SAF-004`, `REQ-SAF-005`, `REQ-SAF-007`;
`docs/architecture/device-protocol.md`; `core/include/pigeon/core/actuator_link.hpp`
(comments only); a planned `protocol/` module; ADR-0007, ADR-0011, ADR-0014,
ADR-0016

## Context

`ActuatorLink` fixes what the Raspberry Pi and the Arduino say to each other —
aim, fire, safe state, a reply to every command that distinguishes acceptance
from refusal, and a periodic device heartbeat — but not how it is encoded
(ADR-0007). `REQ-COM-001` is the only requirement still `UNVERIFIED` for that
reason: there is nothing in `core/` a test could assert against without
inventing an encoding.

Constraints that bear on it:

* **Two devices, two languages of constraint.** The Pi runs C++20 on 64-bit
  Linux; the Arduino has small RAM, no heap to speak of, and a toolchain that
  may not support C++20 or the standard library. Which board it is has not been
  specified.
* **A serial line corrupts bytes.** A water actuator sits on the far end, so a
  corrupt message must be provably rejected, and a lost one must be detected.
* **Safety limits hold on both sides** (`REQ-AIM-002`, `REQ-SAF-001`): the
  device must not trust the host.
* **Testable without hardware** (`REQ-COM-001`, `REQ-DEV-001`, `REQ-DEV-003`),
  and deterministic (`REQ-DEV-002`): no wall clock, no real port in the default
  suite.
* **A fire command is not retriable.** Whether a burst happened is not knowable
  from the host once a reply is lost (`REQ-SAF-007`, ADR-0011).

## Decision

D1 to D3 and D4 are parts of one decision, the design of the device protocol;
D4 records a part deliberately left out.

**D1 — Binary messages, delimited and checked.** One message is
`COBS(body ‖ CRC-16) ‖ 0x00`: a three-byte header (version, type, sequence)
and a fixed-length payload per type, little-endian, angles as signed 16-bit
hundredths of a degree. COBS makes `0x00` an unambiguous delimiter, so a
receiver that has lost its place resynchronises at the next one; CRC-16/CCITT
detects corruption; a hard 16-byte bound caps the receive buffer. Commands run
as stop-and-wait exchanges, with **no retransmission of any command**. The
full encoding is `docs/architecture/device-protocol.md`, including normative
test vectors.

**D2 — Liveness by heartbeat and silence.** The device heartbeats every
**500 ms**; the host declares the link `UNAVAILABLE` after **1.5 s** of silence
(three intervals); a command's reply must arrive within **100 ms** or the
exchange is a `TRANSPORT_FAILURE`. `health()` is derived from received
messages and the injected clock alone, so it transmits nothing
(`REQ-COM-003`). These numbers are the architect's recommendation, adopted on
the maintainer's delegation of 2026-11-26; the requirements fix no value for
any of them.

**D3 — One shared codec, in a new `protocol/` module.** The encoding and
decoding live once, in a header-only module used by both `raspberry/` and
`arduino/`: fixed-width integers from `<cstdint>`, caller-supplied buffers, no
allocation, no exceptions, no virtual functions, and constructs available in
C++17 so common Arduino toolchains can build it. `core/` does not depend on it
(ADR-0007). The module is created in the step that implements the codec, not
here.

**D4 — No host-silence watchdog on the device.** A device that closes its own
valve when the Pi goes quiet was considered and **not adopted**.
`REQ-SAF-001` already requires that every burst ends from the device's own
timer, in at most 500 ms, with no further message, so a silent host can never
leave the valve open longer than that. A software watchdog would cost a new
host-to-device message, a second timer and a new safety requirement to guard
against something already bounded; it would not guard against a faulty burst
timer, because it would depend on firmware timing too; and it would not limit a
host that keeps talking, which the device bounds instead with its own burst
limit and cool-down (the 6-per-minute rate is the host's alone). The mechanism
that *is* independent of firmware faults is the microcontroller's hardware
watchdog, which belongs to the firmware step and is not a protocol feature.

*Provenance.* The maintainer delegated the four decisions to the architect's
recommendations ("your recommendations", 2026-11-26). D1 to D3 follow the
recommendations as put to the maintainer. **D4 does not:** the recommendation
was first drafted as "adopt" and was reversed to "do not adopt" while the
specification was being written, once the burst bound above was seen to cover
the case. The maintainer was told of the reversal and confirmed it the same day
("So your recommendation", 2026-11-26), and also ruled on the one question the
specification raised, Q26: out-of-range angles are **refused**, not clamped.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Text lines (e.g. `AIM -45.00 15.00\n`) | Easy to read on a terminal, but needs a parser for numbers and a checksum scheme on a device with little RAM, and a text parser is a larger surface for malformed input than fixed-length fields. Debuggability is recovered by the test vectors and a decoder. |
| Length-prefixed binary, no byte stuffing | A single corrupted length byte desynchronises the stream until a timeout; COBS resynchronises at the next delimiter. |
| CRC-8, or a sum | A short check on a line that also corrupts is a weaker guarantee than the cost of two more bytes justifies. |
| Retransmit on a lost reply | Unsafe for `FIRE` (a duplicate fires twice) and unnecessary for the rest: the caller abandons the engagement on any failure (`REQ-COM-002`). |
| Separate codecs for host and firmware, checked against shared vectors | Two implementations of one wire format drift. Kept as the **fallback**: if the firmware toolchain cannot build the shared module, the vectors in the specification are normative for exactly this reason. |
| The codec inside `core/` | `core/` models operations, not encodings (ADR-0007); an encoding in `core/` would couple it to one transport. |
| The codec inside `raspberry/` only | The firmware could not use it, which recreates the two-implementations problem. |
| Host-silence watchdog on the device (D4) | See D4: it guards against a case `REQ-SAF-001` already bounds. |
| Derive `health()` from the last exchange's outcome | Rejected by ADR-0016: remembered state nothing forces anyone to refresh. |

## Consequences

### Positive

* `REQ-COM-001` becomes verifiable: the codec is tested against the
  specification's vectors with no serial device, and malformed input has a
  defined rejection at every step.
* Every `LinkStatus` has a defined cause on the wire, so `REQ-SAF-008`'s
  refusals are all reachable from a real link as well as a simulated one.
* One codec, one set of vectors: the Pi and the Arduino cannot silently
  disagree about a byte.
* No change to `core/`'s behaviour, and no new safety requirement.

### Negative / accepted trade-offs

* Fixed-length messages and a version byte mean any layout change is a new
  protocol version and a coordinated update of both devices.
* The heartbeat timings are unproven on real hardware. They are parameters
  (see the configured-parameters table) and the first hardware run may move
  them.
* Out-of-range angles are refused rather than clamped by the device (Q26,
  maintainer ruling). This is fail-closed, and it is why `LinkStatus::REJECTED`
  has a cause that can occur for aiming as well as for firing.
* A shared module constrains the C++ that `protocol/` may use, and the
  firmware toolchain has not been chosen.
* Health can read `OK` for up to about 1.7 s after the device stops: the 1.5 s
  silence timeout tolerates two lost heartbeats, and the host's reader adds up
  to 200 ms. A dead device fires nothing and
  a command sent into it faults, so nothing unsafe results, but the window is
  real and the timeout is a parameter.

### Follow-up work

Each is its own change, test-first:

1. **The codec** (`protocol/`): CRC, COBS and message encode/decode, tested
   against the vectors; clang-tidy and the architecture check extended to the
   new module.
2. **The host link** (`raspberry/`): a `SerialActuatorLink` over an abstract
   byte stream, tested against a simulated device, with `health()` derived as
   in the specification.
3. **The serial port and a hardware test**, kept out of the default suite
   (`REQ-DEV-003`).
4. **The firmware** (`arduino/`): needs the board named and a toolchain
   decision, and cannot be built by the current CI.

## Verification

* The vectors in `docs/architecture/device-protocol.md` become tests in
  follow-up 1; until then `REQ-COM-001` stays reported `UNVERIFIED` by
  `scripts/trace.sh`, which is the intended state of work that is specified
  but not built.
* This ADR creates no code to check. Follow-up 1 adds the `protocol/` module
  and extends `scripts/arch-check.sh` to it, so that "no hardware or platform
  headers" holds there as it does in `core/`.
