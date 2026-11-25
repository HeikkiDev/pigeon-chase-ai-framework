# ADR-0020 — The wire protocol lives in a shared, freestanding `protocol/` module

**Status:** Proposed
**Date:** 2026-11-25
**Affects:** `REQ-COM-001`, `REQ-COM-002`, `REQ-COM-003`, `REQ-SAF-001`,
`REQ-SAF-004`, `REQ-AIM-002`, `REQ-SAF-008`; new module `protocol/`;
`ActuatorLink` (unchanged)

> Numbered 0020 because ADR-0019 is taken by the unmerged `real-detector`
> branch. Open questions raised here start at Q26 for the same reason.

## Context

`REQ-COM-001` requires the Raspberry Pi ↔ Arduino protocol to be specified in
`docs/architecture/` and to be implementable and testable without hardware. It
is the last requirement that has no tests and needs no hardware to test, and it
stays `UNVERIFIED` because nothing encodes or decodes a byte.

`core/` deliberately models the device boundary as operations (`ActuatorLink`,
ADR-0007), not as an encoding. Something must turn those operations into bytes
on a serial line and back. Both ends need exactly the same encoding:

* the **host** (Raspberry Pi, C++20, hosted, 1 GB RAM), and
* the **firmware** (Arduino, AVR-class: `int` is 16 bits, no heap, no
  exceptions, no RTTI, no STL containers; `arduino.instructions.md`).

`arduino.instructions.md` already asks that parsing and framing "can be
compiled and unit tested on the host as well as on the device", free of
`Arduino.h`.

Two independent copies of a codec, one per device, would be two sources of
truth about one format. They would drift, and a drift here means the safety
device misreading a command.

Constraints:

* Hardware independence (`REQ-DEV-001`): the codec must build and test on macOS
  with no serial port.
* Determinism (`REQ-DEV-002`): no clock, no randomness inside the codec.
* The firmware enforces its own limits (`REQ-SAF-001`, `REQ-AIM-002`); the
  protocol must carry enough for it to refuse, and for the host to learn that
  it refused.
* `health()` must be answerable without an exchange (`REQ-COM-003`), so the
  device must speak unprompted: a heartbeat.
* The `real-detector` branch builds `raspberry/` only when ncnn is installed.
  Protocol tests placed under `raspberry/` would silently not run on a machine
  without ncnn.

## Decision

**The wire protocol is implemented once, in a new top-level module
`protocol/`, which both the host and the firmware compile.**

1. **Location and dependencies.** `protocol/include/pigeon/protocol/` and
   `protocol/src/`. It depends on nothing: not on `core/`, not on the standard
   library beyond `<cstdint>` and `<cstddef>`. Dependency direction:

   ```text
   raspberry/ ──▶ core/
   raspberry/ ──▶ protocol/ ◀── arduino/
   tests/     ──▶ core/, protocol/
   ```

   Conversion between `core` types (`Angle` in `double` degrees,
   `std::chrono::milliseconds`) and wire integers happens in the host adapter
   in `raspberry/`, not in `protocol/`, so the firmware never sees `double` or
   `<chrono>`.

2. **Freestanding subset.** Code in `protocol/` uses only fixed-width integers,
   fixed-size buffers owned by the caller, no allocation, no exceptions, no
   RTTI, no virtual functions, no floating point. It must compile under the
   C++ standard the Arduino toolchain supports (to be confirmed, see Q28).

3. **Framing** (proposed, to be specified fully in
   `docs/architecture/protocol.md`):

   ```text
   SYNC (0xA5) | TYPE (u8) | SEQ (u8) | LEN (u8, ≤ 8) | PAYLOAD (LEN bytes) | CRC-16 (big-endian)
   ```

   CRC-16/CCITT-FALSE over `TYPE..PAYLOAD`. Multi-byte integers are
   big-endian. A receiver that sees a bad length or CRC discards the frame and
   resynchronises on the next `SYNC`; it never acts on a partial frame.

4. **Messages** (proposed):

   | Direction       | Type        | Payload                                  | Purpose                         |
   | --------------- | ----------- | ---------------------------------------- | ------------------------------- |
   | host → device   | `AIM`       | x, y: `int16` centidegrees               | `send_aiming_command`           |
   | host → device   | `FIRE`      | duration: `uint16` ms                    | `send_fire_command`             |
   | host → device   | `SAFE`      | none                                     | `send_safe_state_command`       |
   | device → host   | `ACK`       | echoed `SEQ`                             | command accepted                |
   | device → host   | `NAK`       | echoed `SEQ`, reason (`u8`)              | command understood and refused  |
   | device → host   | `HEARTBEAT` | counter (`u16`), actuator-active flag    | liveness for `health()`         |

5. **Mapping to `LinkStatus`** (the host adapter's job, stated here so both
   sides agree):

   | Observation on the host                                 | `LinkStatus`        |
   | ------------------------------------------------------- | ------------------- |
   | `ACK` with matching `SEQ`                                | `OK`                |
   | `NAK` with matching `SEQ`                                | `REJECTED`          |
   | Bad CRC, bad length, unknown type, wrong `SEQ`, no reply within the reply timeout | `TRANSPORT_FAILURE` |
   | Port closed, or no `HEARTBEAT` within the heartbeat timeout | `UNAVAILABLE`       |

6. **No retransmission of `FIRE`.** A transmitted fire command counts whatever
   the reply (`REQ-SAF-007`, ADR-0011); resending it after a lost `ACK` could
   double a burst. The host never retries `FIRE`. Whether `AIM` and `SAFE` may
   be retried is left open (not needed for `REQ-COM-001`).

7. **Decoder shape.** An incremental, byte-at-a-time state machine over a
   caller-owned buffer, returning a value (`frame ready`, `need more`,
   `discarded: <reason>`). Every input sequence of any length is accepted
   without undefined behaviour. The same decoder runs on both sides.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Codec in `raspberry/` and again in `arduino/`, as `REQ-COM-001`'s note assumed | Two copies of one format drift. The firmware copy could only be tested on the board, so its malformed-input handling would be unverified in the default suite. |
| Codec in `core/` | `core/` is the hosted domain layer (`double`, `<chrono>`, `std::optional`). Making it compile for AVR would constrain all of it for the sake of one file. The encoding is also not domain logic: ADR-0007 deliberately kept it out. |
| Codec in `raspberry/` only, firmware hand-written | Same drift problem. Also, on the `real-detector` branch, `raspberry/` builds only with ncnn, so the protocol tests would be skipped on machines without ncnn. |
| Text protocol (e.g. `AIM 1234 -567\n`) | Easy to read with a terminal, but needs number parsing in firmware, has no natural integrity check, and makes "malformed" a much larger space to test. Can be revisited if debugging needs it. |
| COBS framing instead of a sync byte | Stronger resynchronisation, but more code and a second concept to explain. A sync byte with a length and a CRC is sufficient at these message sizes. Revisit if field tests show resync problems. |
| A third-party serialisation library (nanopb, msgpack) | A new dependency (`AGENTS.md` rule 12) for six messages of at most 8 bytes. Not justified. |

## Consequences

### Positive

* One encoding, compiled by both devices. The firmware's parser is tested in
  the default suite on macOS.
* `REQ-COM-001` becomes verifiable: encoding, decoding and malformed-input
  rejection are pure functions over bytes.
* Independent of the `real-detector` branch: no file it touches is needed, and
  `protocol/` tests do not depend on ncnn.
* `core/` and `ActuatorLink` are unchanged.

### Negative / accepted trade-offs

* A fourth top-level source module, with its own (stricter) coding rules.
* `REQ-COM-001`'s explanatory note ("implemented in `raspberry/` and
  `arduino/`") needs rewording. Its **statement** and acceptance criteria are
  unchanged.
* Centidegrees in `int16` give ±327.67° at 0.01° resolution. Enough for the
  envelope (X ∈ [−90°, +90°], Y ∈ [0°, +45°]), but it is a decision about
  precision (Q27).
* Until `arduino/` exists, "the firmware compiles it" is a promise. The
  freestanding rule is enforced by the gate (see Verification), not by an AVR
  build.

### Follow-up work

* Specify the protocol in full in `docs/architecture/protocol.md`.
* Answer Q26–Q31 (below) before the values they block are written down.
* Host adapter `SerialActuatorLink` in `raspberry/` (needs a serial transport
  seam; separate ADR or an extension of this one).
* Firmware in `arduino/`.
* When `real-detector` merges, reconcile the `raspberry/` CMake gating so the
  serial adapter does not depend on ncnn.

## Open questions raised

These are added to the Open Questions table in
`docs/requirements/requirements.md`. None is answered here.

| #   | Question | Blocks |
| --- | -------- | ------ |
| Q26 | **Clamp or refuse out-of-envelope angles in firmware?** `ActuatorLink`'s header says the receiver "clamps them again"; `LinkStatus::REJECTED` says the device refuses "angles outside its own envelope". Both cannot be the behaviour. | `NAK` reasons; `REQ-AIM-002` on the device side |
| Q27 | **Angle resolution on the wire.** Is 0.01° (centidegrees in `int16`) acceptable? | `AIM` payload |
| Q28 | **Arduino board and toolchain.** Which board (AVR Uno/Nano, or ARM/ESP32) and which C++ standard does its toolchain support? | The freestanding subset `protocol/` must compile under |
| Q29 | **Heartbeat interval and host timeout.** How often does the device send `HEARTBEAT`, and after how long without one does `health()` report `UNAVAILABLE`? | `REQ-COM-003` on real hardware |
| Q30 | **Reply timeout.** How long does the host wait for `ACK`/`NAK` before reporting `TRANSPORT_FAILURE`? | `send_*` on real hardware |
| Q31 | **Firmware's own fire bound.** Is the firmware's independent maximum burst also 500 ms, or a different (larger, hard) ceiling above the host's configured maximum? | `REQ-SAF-001` third acceptance bullet; `NAK` vs truncation |

Also open, but not blocking this ADR: serial baud rate (a deployment value),
and whether `AIM`/`SAFE` may be retried.

## Verification

* **Tests** in `tests/protocol/`, in the default suite, naming `REQ-COM-001`:
  round-trip encode/decode of every message; rejection of bad CRC, bad length,
  unknown type and truncated frames; resynchronisation after garbage; a
  seeded, deterministic random-bytes test asserting the decoder never reads or
  writes outside its buffer (run under `make asan`).
* **`scripts/arch-check.sh`** extended, test-first in `tests/scripts/`, to keep
  `protocol/` free of hardware headers, I/O headers, allocation (`new`,
  `malloc`, `<vector>`, `<string>`, `<memory>`), `<chrono>`, floating point and
  any `pigeon/core` include.
* **Compile flags:** `protocol/` built with `-fno-exceptions -fno-rtti` in the
  default build, so the freestanding rule is checked by the compiler on every
  `make check`.
* **Traceability:** `REQ-COM-001` moves from `UNVERIFIED` to verified in
  `verified.txt`.
