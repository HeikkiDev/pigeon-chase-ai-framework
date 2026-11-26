# Device protocol — Raspberry Pi ↔ Arduino (version 1)

**Status:** specified, not yet implemented. Implementation is staged in
ADR-0021; until the codec and both sides exist, `REQ-COM-001` stays
`UNVERIFIED`.
**Satisfies:** `REQ-COM-001`. Carries `REQ-COM-002`, `REQ-COM-003`,
`REQ-AIM-002`, `REQ-SAF-001`, `REQ-SAF-004`, `REQ-SAF-005`, `REQ-SAF-007`.
**Decided in:** ADR-0021.

This document is the wire encoding of the `ActuatorLink` interface
(`core/include/pigeon/core/actuator_link.hpp`). That interface says *what* the
two devices exchange; this document says *which bytes*. Nothing here changes
what `core/` does: `core/` still sees only operations and a `LinkStatus`
(ADR-0007).

The key words SHALL, MAY and SHALL NOT carry the meaning they have in
`docs/requirements/requirements.md`.

## 1. Vocabulary

A **message** is the unit on the wire: one command, reply or heartbeat. It is
not a camera *frame* — the glossary reserves that word for images, and the two
must not meet. The **host** is the Raspberry Pi; the **device** is the
Arduino.

## 2. Transport

The protocol runs over any reliable-order byte stream that may corrupt, drop or
duplicate bytes but does not reorder them. In deployment that is a serial line.
The reference setting is **115200 baud, 8 data bits, no parity, 1 stop bit**;
it is a deployment setting of the transport, not part of the protocol, and the
protocol does not depend on it. Nothing here assumes a particular Arduino board.

## 3. Message format

On the wire, one message is:

```text
COBS( body ‖ crc16 )  ‖  0x00
```

* `body` is the header and payload below.
* `crc16` is the CRC of `body`, two bytes, **little-endian**.
* `COBS` is Consistent Overhead Byte Stuffing, which removes every `0x00` from
  its input, so that `0x00` is an unambiguous message delimiter.
* The final `0x00` is the delimiter. A receiver that has lost its place
  resynchronises at the next `0x00`.

### 3.1 Body

| Offset | Size | Field     | Meaning                                                           |
| ------ | ---- | --------- | ----------------------------------------------------------------- |
| 0      | 1    | `version` | Protocol version. This document is version **1**.                 |
| 1      | 1    | `type`    | Message type (section 4).                                         |
| 2      | 1    | `seq`     | Sequence number, `0…255`, wrapping. Meaning depends on the type.  |
| 3      | n    | payload   | Fixed length for each type; no padding, no optional fields.       |

### 3.2 Integers and units

* Every multi-byte integer is **little-endian**, including the CRC.
* Angles are **signed 16-bit integers in hundredths of a degree** (`−4500` is
  −45.00°), in the servo frame of `REQ-AIM-002`. Integers keep the device free
  of floating point. The host rounds **towards zero**, so rounding cannot take
  an angle outside an envelope whose bounds are whole hundredths.
* Durations are **unsigned 16-bit integers in milliseconds**.

### 3.3 CRC

CRC-16/CCITT-FALSE: polynomial `0x1021`, initial value `0xFFFF`, no input or
output reflection, no final XOR. Check value: the CRC of the ASCII string
`123456789` is `0x29B1`. It covers `body` only.

### 3.4 Size

The largest body is 7 bytes (3 header + 4 payload, for `AIM`). With the 2-byte
CRC that is 9; COBS adds one byte and the delimiter one more, so a message is
at most **11 bytes** on the wire. A receiver SHALL discard any message longer than
**16 bytes** before its delimiter, and SHALL NOT keep buffering beyond that
bound: the buffer is fixed and small, and an unbounded one is a way to crash a
receiver with noise.

## 4. Message types

### 4.1 Host → device (commands)

| Type   | Name         | Payload                                        | Length | `seq`                                  |
| ------ | ------------ | ---------------------------------------------- | ------ | -------------------------------------- |
| `0x01` | `AIM`        | `x` `i16`, `y` `i16` (hundredths of a degree)  | 4      | Chosen by the host; echoed in the reply |
| `0x02` | `FIRE`       | `duration_ms` `u16`                            | 2      | Chosen by the host; echoed in the reply |
| `0x03` | `SAFE_STATE` | none                                           | 0      | Chosen by the host; echoed in the reply |

These are `send_aiming_command`, `send_fire_command` and
`send_safe_state_command`. Type `0x04` is reserved and unused.

### 4.2 Device → host

| Type   | Name        | Payload                                  | Length | `seq`                                   |
| ------ | ----------- | ---------------------------------------- | ------ | --------------------------------------- |
| `0x81` | `REPLY`     | `command_type` `u8`, `result` `u8`       | 2      | The `seq` of the command it answers     |
| `0x82` | `HEARTBEAT` | `flags` `u8`                             | 1      | The device's own counter; wraps         |

`REPLY.command_type` repeats the `type` of the command being answered, so the
host can check that a reply matches what it sent.

`HEARTBEAT.flags`: bit 0 is **`actuator_active`** (the water actuator is
active now); bits 1–7 are reserved, sent as zero, and ignored by the receiver.

### 4.3 Result codes

| `result` | Name                    | Meaning                                                                          |
| -------- | ----------------------- | -------------------------------------------------------------------------------- |
| `0`      | `ACCEPTED`              | The device understood the command and carried it out (or began to).              |
| `1`      | `REJECTED_ANGLE`        | An `AIM` angle lies outside the device's own envelope. The servos do not move.   |
| `2`      | `REJECTED_DURATION`     | A `FIRE` duration exceeds the device's own bound. The actuator is not activated. |
| `3`      | `REJECTED_COOLDOWN`     | A `FIRE` arrived inside the device's own cool-down. The actuator is not activated. |
| `4`      | `REJECTED_UNSUPPORTED`  | A message with a valid CRC that the device cannot interpret (section 5).         |

A `FIRE` with `duration_ms = 0` is `ACCEPTED` and activates nothing: a zero
duration is a command that does nothing (`FireCommand`).

## 5. Receiving a message

Both sides apply these steps in order to every message, and stop at the first
that fails:

| Step | Check                                                                  | On failure                                                                        |
| ---- | ---------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| 1    | The message fits the 16-byte bound and its COBS encoding is well formed | Discard. No reply.                                                                |
| 2    | At least 5 bytes decode (3 header + 2 CRC), and the CRC matches        | Discard. No reply.                                                                |
| 3    | `version` is 1                                                         | **Device:** reply `REJECTED_UNSUPPORTED`. **Host:** a fault (section 7).          |
| 4    | `type` is one this side receives                                       | As step 3.                                                                        |
| 5    | The payload length is exactly the length for that type                 | As step 3.                                                                        |
| 6    | *(device only)* the values are within the device's own limits          | Reply with the matching `REJECTED_*` (section 4.3). The command is not carried out. |

Rules that follow from this:

* **Nothing from a failed step 1 or 2 is ever acted on or answered.** A
  corrupt message has no trustworthy `seq`, so no reply could be matched to
  anything. The sender finds out by the reply timeout (section 6).
* An empty message — two delimiters in a row — is ignored. It is not a fault.
* The device sends `REJECTED_UNSUPPORTED` with the `seq` and `type` it
  received. The host never replies to anything.
* A message that passes steps 1–5 is **valid**, whatever step 6 decides. A
  valid message proves the sender is alive and the line is carrying messages.
* Malformed input is rejected without undefined behaviour: every step reads
  only bytes already received and checked against the bounds above.

### 5.1 Device limits (step 6)

The device enforces its own limits, independently of the host
(`REQ-AIM-002`, `REQ-SAF-001`, `REQ-SAF-005`). The host's clamping is a first
line; this is a second, because a defect on the commanding side must not be
able to move the rig outside its envelope or hold the valve open. The
device's limits SHALL NOT be wider than the deployment values:

* **Angles:** X ∈ [−90°, +90°], Y ∈ [0°, +45°]. An out-of-range angle is
  refused (`REJECTED_ANGLE`) and the servos **do not move**: failing closed is
  chosen over clamping and moving, and Q26 in the requirements asks the
  maintainer to confirm it.
* **Burst:** at most 500 ms. A longer request is refused (`REJECTED_DURATION`),
  never truncated.
* **Cool-down:** at least 2 s after a burst, by the same elapsed-time
  convention as the host (ADR-0014). A `FIRE` inside it is `REJECTED_COOLDOWN`.
* **Self-termination:** an accepted burst ends by itself, from the device's
  own timer, with no further message and no dependence on the host being alive
  (`REQ-SAF-001`).
* **Safe state:** the device powers up with the water actuator inactive
  (`REQ-SAF-004`), and `SAFE_STATE` deactivates it at once. `SAFE_STATE` is
  always `ACCEPTED` and is the cheapest operation the device offers.

## 6. Exchanges

A command and its reply are an **exchange**. The host runs exchanges one at a
time (stop-and-wait): it sends a command and does not send another until the
reply arrives or the reply timeout passes.

* The host picks `seq` for each command, incrementing by one and wrapping.
* A reply answers the outstanding command if and only if its `seq` **and**
  `command_type` match. Any other reply — late, duplicated, or for a command
  already given up on — is ignored. It still counts as a valid device message
  for liveness (section 7), but it is not a fault.
* If no matching reply arrives within the **reply timeout** (100 ms), the
  exchange has faulted.
* **The host SHALL NOT retransmit a command.** A retransmitted `FIRE` whose
  first copy was in fact received would fire twice, and a lost reply is
  indistinguishable from a lost command. Whether anything was sent is not
  knowable from the host's side of the line, which is why a transmitted fire
  command counts whatever the outcome (`REQ-SAF-007`, ADR-0011). Failure is
  reported to the caller, which abandons the engagement (`REQ-COM-002`).

### 6.1 What a command returns

| What the host observes                                                      | `LinkStatus` returned by the `send_*` call |
| --------------------------------------------------------------------------- | ------------------------------------------ |
| A matching `REPLY` with `result = 0`                                        | `OK`                                       |
| A matching `REPLY` with `result` 1–4                                        | `REJECTED`                                 |
| A write error; no matching reply within the reply timeout; a reply that fails a receiving step | `TRANSPORT_FAILURE`        |
| The line is not open when the command is due to be sent                     | `UNAVAILABLE` (nothing is transmitted)     |

`SAFE_STATE` is attempted whenever the line is open, **even when health is not
`OK`**: it is the operation that must work when nothing else does.

## 7. Liveness and link health

The device sends a `HEARTBEAT` every **500 ms**, from power-up, whether or not
it has been spoken to. The host's picture of the link therefore stays current
without a fire command being sent to refresh it (`REQ-COM-001`,
`REQ-COM-003`).

The **link silence timeout** is **1.5 s**: three heartbeat intervals, so one or
two lost heartbeats do not declare the link down, and three do. It is judged by
the elapsed-time convention of ADR-0014: silence has lasted the timeout when
`elapsed >= 1.5 s`. Time comes from the injected `MonotonicClock`
(`REQ-DEV-002`), never from the wall clock.

`health()` is answered by the host from this state alone and transmits nothing
(`REQ-COM-003`). It returns the first of these that holds:

| Order | `LinkStatus`        | Holds when                                                                                                                          |
| ----- | ------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| 1     | `UNAVAILABLE`       | The line is not open; or no valid device message has arrived since it opened; or the last valid device message is at least the silence timeout old. |
| 2     | `TRANSPORT_FAILURE` | An exchange has faulted, or a message has failed a receiving step (section 5), and **no valid device message has arrived since**. |
| 3     | `REJECTED`          | The most recent reply to a command was a rejection, and **no accepted reply has arrived since**.                                    |
| 4     | `OK`                | None of the above.                                                                                                                  |

Consequences worth stating:

* A line that has just opened is `UNAVAILABLE`, not `OK`: nothing is known
  about the device until it has been heard. A host cannot fire into silence.
* One corrupted heartbeat makes health `TRANSPORT_FAILURE` until the next good
  one, at most 500 ms later. Noise during an engagement refuses the burst; that
  is intended (`REQ-SAF-008`).
* A device that answers every command with a rejection keeps health at
  `REJECTED` and the system does not fire, rather than continuing to try.
* The `actuator_active` heartbeat flag is for the host's diagnostics and its
  logs. It does not enter `health()`.

Something has to read the line for health to move between commands: the host
implementation consumes incoming bytes independently of sending. How it does so
is the implementation's business, not the protocol's.

## 8. Sequences

**Startup.** The device powers up with the actuator inactive and begins
heartbeating. The host opens the line (health `UNAVAILABLE`), sends
`SAFE_STATE`, and health becomes `OK` on the first valid device message.

**An engagement.** `AIM` (reply `ACCEPTED`) for each aiming frame; then, at the
firing frame, `health()` is consulted and, if `OK`, `FIRE` is sent
(`REQ-SAF-002`, `REQ-SAF-008`). The reply returns before the burst ends; the
burst then ends by itself on the device.

**A silent line.** No valid device message for 1.5 s: health is `UNAVAILABLE`,
the host refuses to fire and abandons any engagement (`REQ-COM-002`). The
device needs to do nothing: it was never relying on the host to close the
valve.

**Shutdown.** The host sends `SAFE_STATE`, then closes the line.

## 9. Parameters

| Parameter                   | Default  | Notes                                                       |
| --------------------------- | -------- | ----------------------------------------------------------- |
| Device heartbeat interval   | 500 ms   | Device-side timer.                                          |
| Link silence timeout        | 1.5 s    | Host-side; three heartbeat intervals.                       |
| Reply timeout               | 100 ms   | Host-side. Under one frame period at 5 FPS (`REQ-DET-003`). |
| Maximum message length      | 16 bytes | Before the delimiter; larger is discarded.                  |
| Serial line setting         | 115200 8N1 | Deployment setting of the transport, not of the protocol. |

They are listed with the other tunable values in the requirements document.

## 10. Test vectors

These are **normative**: an implementation that does not reproduce them is not
this protocol. They were produced by an independent reference implementation
whose COBS encoder was first checked against the published COBS examples and
whose CRC against the check value in section 3.3. Bytes are hexadecimal; each
message ends with the `00` delimiter.

### 10.1 Messages that must encode and decode exactly

| #  | Message                                              | Body (before COBS)            | On the wire                              |
| -- | ---------------------------------------------------- | ----------------------------- | ---------------------------------------- |
| V1 | `AIM` seq 7, X −45.00°, Y +15.00°                    | `01 01 07 6C EE DC 05 E4 C6`  | `0A 01 01 07 6C EE DC 05 E4 C6 00`       |
| V2 | `AIM` seq 0, X 0, Y 0 (zero bytes inside the body)   | `01 01 00 00 00 00 00 0F 0C`  | `03 01 01 01 01 01 01 03 0F 0C 00`       |
| V3 | `FIRE` seq 8, 500 ms                                 | `01 02 08 F4 01 B0 30`        | `08 01 02 08 F4 01 B0 30 00`             |
| V4 | `SAFE_STATE` seq 9                                   | `01 03 09 D6 3F`              | `06 01 03 09 D6 3F 00`                   |
| V5 | `REPLY` seq 7 to `AIM`: `ACCEPTED`                   | `01 81 07 01 00 70 A6`        | `05 01 81 07 01 03 70 A6 00`             |
| V6 | `REPLY` seq 8 to `FIRE`: `REJECTED_DURATION`         | `01 81 08 02 02 50 FF`        | `08 01 81 08 02 02 50 FF 00`             |
| V7 | `HEARTBEAT` seq 0, flags 0                           | `01 82 00 00 4E A7`           | `03 01 82 01 03 4E A7 00`                |
| V8 | `HEARTBEAT` seq 255, `actuator_active`               | `01 82 FF 01 90 B4`           | `07 01 82 FF 01 90 B4 00`                |

### 10.2 Input a receiver must reject

| #  | Input on the wire                                  | What it is                                                | Device behaviour                                              |
| -- | -------------------------------------------------- | --------------------------------------------------------- | ------------------------------------------------------------- |
| N1 | `0A 01 01 06 6C EE DC 05 E4 C6 00`                 | V1 with one bit flipped (CRC mismatch)                    | Discard. No reply.                                            |
| N2 | `06 01 02 08 F4 01 03 53 86 00`                    | `FIRE` seq 8 with a 3-byte payload                        | Reply R2.                                                     |
| N3 | `06 01 7F 03 A8 D3 00`                             | Unknown type `7F`, seq 3                                  | Reply R3.                                                     |
| N4 | `06 02 03 04 2B B7 00`                             | Version 2, `SAFE_STATE` seq 4                             | Reply R4.                                                     |
| N5 | `08 01 01 05 01 02 6A 35 00`                       | `AIM` seq 5 with a 2-byte payload                         | Reply R5.                                                     |
| N6 | `11` × 17, then `00`                               | 17 bytes before the delimiter: over the 16-byte bound     | Discard. No reply.                                            |
| N7 | `05 11 22 00`                                      | COBS code byte promises more bytes than the message holds | Discard. No reply.                                            |
| N8 | `00`                                               | A lone delimiter: an empty message                        | Ignore. Not a fault.                                          |
| N9 | `08 01 02 08 F5 01 81 03 00`                       | `FIRE` seq 8, 501 ms — well formed, over the device bound | Reply V6; the actuator is not activated.                      |

| #  | The reply to it                                    | On the wire                              |
| -- | -------------------------------------------------- | ---------------------------------------- |
| R2 | `REPLY` seq 8, `FIRE`, `REJECTED_UNSUPPORTED`      | `08 01 81 08 02 04 96 9F 00`             |
| R3 | `REPLY` seq 3, type `7F`, `REJECTED_UNSUPPORTED`   | `08 01 81 03 7F 04 62 11 00`             |
| R4 | `REPLY` seq 4, `SAFE_STATE`, `REJECTED_UNSUPPORTED` | `08 01 81 04 03 04 C6 D9 00`            |
| R5 | `REPLY` seq 5, `AIM`, `REJECTED_UNSUPPORTED`       | `08 01 81 05 01 04 94 88 00`             |

The same inputs, received by the **host** in place of the device, must make it
discard (N1, N6, N7), ignore (N8), or record a fault (N2–N5, which a host would
receive only from a broken or incompatible device). A host never replies.

## 11. Versioning

`version` is carried in every message so that a mismatch is detected on the
first one rather than inferred from garbled behaviour. There is no negotiation:
a receiver that does not speak the version refuses it (section 5), and since the
host then sees a fault on every heartbeat, health never reaches `OK` and nothing
fires. **Any change to a message layout, a result code's meaning or a timing
default is a new version.** Adding a new type is a new version too, so that an
old receiver cannot meet a type it half-understands.

## 12. Requirement coverage

| Requirement   | Where it is met                                                                               |
| ------------- | --------------------------------------------------------------------------------------------- |
| `REQ-COM-001` | This document; encode/decode tests need no serial device (sections 3, 10); malformed input is rejected (section 5); every command is answered with acceptance or refusal (4.3, 6) and the device heartbeats (7). |
| `REQ-COM-002` | A silent, failing or closed line yields `UNAVAILABLE` or `TRANSPORT_FAILURE` (6.1, 7); the host abandons the engagement. |
| `REQ-COM-003` | `health()` is derived from received messages and the clock alone, never from an exchange (7). |
| `REQ-AIM-002` | The device re-checks angles against its own envelope (5.1).                                   |
| `REQ-SAF-001` | The device bounds and self-terminates every burst (5.1).                                      |
| `REQ-SAF-004` | Power-up and `SAFE_STATE` leave the actuator inactive (5.1, 8).                               |
| `REQ-SAF-005` | The device enforces its own cool-down (5.1).                                                  |
| `REQ-SAF-007` | A transmitted `FIRE` is never retransmitted, and its outcome does not change that it counts (6). |
| `REQ-SAF-008` | Each non-`OK` status is reachable from `health()` (7).                                        |

## 13. Not specified here

* The serial port implementation, and the host's reader and timers
  (`raspberry/`).
* The firmware (`arduino/`), including which board it runs on.
* The codec's source layout (ADR-0021 plans a shared, header-only module).
* A host-silence watchdog on the device. It was considered and not adopted: see
  ADR-0021.
