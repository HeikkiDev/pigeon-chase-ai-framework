# Wire Protocol — Raspberry Pi ↔ Arduino

The byte-level specification of the device boundary (`REQ-COM-001`). It is the
encoding of the operations `ActuatorLink` defines (`core/include/pigeon/core/actuator_link.hpp`):
that header says *what* the two devices do for each other, this document says
*how the bytes look*. Design and rationale: ADR-0020. Implemented once, in
`protocol/`, and compiled by both devices.

Normative words (SHALL, SHALL NOT, MAY) are used as in
`docs/requirements/requirements.md`.

## 1. Roles and transport

* The **host** is the Raspberry Pi. The **device** is the Arduino Uno (Q28).
* Transport: USB serial, 8 data bits, no parity, 1 stop bit (8N1). The baud
  rate is a deployment value and is not specified here.
* **Stop-and-wait.** The host SHALL have at most one command outstanding. It
  SHALL NOT send a command until the previous one has been answered or has
  timed out (§6).
* The device speaks unprompted only to send `HEARTBEAT` (§4.6).

## 2. Packet

Every message, in both directions, is one packet:

| Offset    | Field     | Size     | Value                                          |
| --------- | --------- | -------- | ---------------------------------------------- |
| 0         | `SYNC`    | 1        | `0xA5`                                         |
| 1         | `TYPE`    | 1        | Message type (§3)                              |
| 2         | `SEQ`     | 1        | Sequence number (§5)                           |
| 3         | `LEN`     | 1        | Payload length in bytes, `0` to `8`            |
| 4         | `PAYLOAD` | `LEN`    | Message payload (§4)                           |
| 4 + `LEN` | `CRC`     | 2        | CRC-16 over `TYPE`, `SEQ`, `LEN` and `PAYLOAD`, big-endian |

* Shortest packet: 6 bytes (`LEN = 0`). Longest: 14 bytes (`LEN = 8`).
* **Integers** in payloads are big-endian. Signed integers are two's complement.
* **CRC:** CRC-16/CCITT-FALSE. Polynomial `0x1021`, initial value `0xFFFF`,
  input and output not reflected, final XOR `0x0000`. Check value: the CRC of
  the ASCII bytes `123456789` is `0x29B1`. `SYNC` and the CRC itself are not
  covered.
* `0xA5` MAY appear inside a packet (in any field after `SYNC`). It has no
  special meaning there; the receiver knows where the packet ends from `LEN`.

## 3. Message types

The top bit of `TYPE` gives the direction: clear for host → device, set for
device → host.

| `TYPE` | Name        | Direction       | `LEN` | Answers / operation                 |
| ------ | ----------- | --------------- | ----- | ----------------------------------- |
| `0x01` | `AIM`       | host → device   | 4     | `send_aiming_command`               |
| `0x02` | `FIRE`      | host → device   | 2     | `send_fire_command`                 |
| `0x03` | `SAFE`      | host → device   | 0     | `send_safe_state_command`           |
| `0x81` | `ACK`       | device → host   | 0     | Command accepted                    |
| `0x82` | `NAK`       | device → host   | 1     | Command understood and refused      |
| `0x83` | `HEARTBEAT` | device → host   | 3     | Liveness, for `health()`            |

Every other `TYPE` value is reserved. Each type has exactly one valid `LEN`;
any other `LEN` makes the packet malformed (§7).

## 4. Payloads

### 4.1 `AIM` (`0x01`)

| Offset | Field | Type    | Meaning                                    |
| ------ | ----- | ------- | ------------------------------------------ |
| 0      | `x`   | `int16` | Pan angle in centidegrees (0.01°, Q27)     |
| 2      | `y`   | `int16` | Tilt angle in centidegrees (0.01°, Q27)    |

* Angles are in the servo frame (`REQ-AIM-002`): X = 0 straight ahead,
  positive X to the right, Y = 0 the horizon, positive Y elevated.
* The host converts `Angle` (`double` degrees) to centidegrees in its adapter,
  rounding to the nearest integer. `protocol/` never sees a `double`.
* **Device:** if both angles lie within the device's own envelope it SHALL move
  the servos and reply `ACK`. Otherwise it SHALL NOT move either servo and SHALL
  reply `NAK` with reason `ANGLE_OUT_OF_ENVELOPE` (Q26). It never clamps.
* The device's envelope is the deployment envelope, inclusive:
  X ∈ [−9000, +9000], Y ∈ [0, +4500] centidegrees (`REQ-AIM-002`).

### 4.2 `FIRE` (`0x02`)

| Offset | Field         | Type     | Meaning                         |
| ------ | ------------- | -------- | ------------------------------- |
| 0      | `duration_ms` | `uint16` | Requested burst length, in ms   |

* **Device:** activates the water actuator for
  `min(duration_ms, 500)` ms and replies `ACK`. A request above 500 ms is
  **truncated** to 500 ms, not refused (Q31, Q32). The burst ends on the
  device's own timer, with no further command (`REQ-SAF-001`).
* `duration_ms = 0` activates nothing and is answered with `ACK`. This matches
  `FireCommand`'s default of zero.
* **The host SHALL NOT retransmit `FIRE`**, whatever happens to the reply
  (ADR-0020 §6, ADR-0011).
* A `FIRE` received while a burst is already active SHALL be **ignored**: the
  device replies `ACK` and the running burst keeps its original end time. It
  is neither extended nor restarted (Q33), so no sequence of `FIRE`s can keep
  the water on longer than one bound.

### 4.3 `SAFE` (`0x03`)

No payload. **Device:** deactivates the water actuator immediately, ending any
active burst, holds the servos where they are, and replies `ACK`
(`REQ-SAF-004`). `SAFE` SHALL always be accepted; it has no `NAK` reason.

### 4.4 `ACK` (`0x81`)

No payload. `SEQ` is the `SEQ` of the command it answers.

### 4.5 `NAK` (`0x82`)

| Offset | Field    | Type    | Meaning           |
| ------ | -------- | ------- | ----------------- |
| 0      | `reason` | `uint8` | Why it was refused |

`SEQ` is the `SEQ` of the command it answers.

| `reason` | Name                    | Sent for                           |
| -------- | ----------------------- | ---------------------------------- |
| `0x01`   | `ANGLE_OUT_OF_ENVELOPE` | `AIM` outside the device envelope (Q26) |

Every other `reason` value is reserved. A `NAK` with a reserved `reason` is
malformed (§7).

### 4.6 `HEARTBEAT` (`0x83`)

| Offset | Field     | Type     | Meaning                                                  |
| ------ | --------- | -------- | -------------------------------------------------------- |
| 0      | `counter` | `uint16` | Increments by one per heartbeat, wrapping 65535 → 0      |
| 2      | `flags`   | `uint8`  | Bit 0: water actuator active. Bits 1–7 reserved, SHALL be 0 |

* `SEQ` SHALL be `0`.
* The device SHALL send a `HEARTBEAT` every **100 ms** (Q29), whether or not
  commands are arriving.
* A `HEARTBEAT` with a non-zero `SEQ` or a reserved flag bit set is
  malformed (§7).

## 5. Sequence numbers

* The host gives each command a `SEQ`, incrementing by one per command and
  wrapping 255 → 0. Its first command MAY use any value.
* The device copies the command's `SEQ` into its `ACK` or `NAK`.
* A reply whose `SEQ` does not match the outstanding command is **not** its
  reply. The host discards it and keeps waiting until the reply timeout (§6).
  This stops a late reply to an earlier, timed-out command from being taken as
  the answer to the current one.

## 6. Timing

| Rule                 | Value      | Source |
| -------------------- | ---------- | ------ |
| Heartbeat interval   | 100 ms     | Q29    |
| Heartbeat timeout    | 500 ms     | Q29    |
| Reply timeout        | 50 ms      | Q30    |
| Device fire bound    | 500 ms     | Q31    |

* **Reply timeout:** measured from the end of transmitting a command. With no
  matching `ACK` or `NAK` within 50 ms, the command's outcome is
  `TRANSPORT_FAILURE`.
* **Heartbeat timeout:** measured from the last valid `HEARTBEAT` received.
  Only a `HEARTBEAT` resets it; `ACK` and `NAK` do not.
* Timing is the host adapter's and the firmware's concern. `protocol/` has no
  clock (`REQ-DEV-002`); its tests do not involve time.

## 7. Malformed input

A received packet is **malformed** if any of these holds:

* `LEN` is greater than 8.
* `TYPE` is reserved, or is a type the receiver never receives (a device
  receiving `ACK`, a host receiving `AIM`).
* `LEN` is not the one valid length for `TYPE` (§3).
* The CRC does not match.
* A reserved field value is used (§4.5, §4.6).

The receiver checks each condition as soon as it has the bytes to do so, and
reports the packet malformed at that byte:

| Condition                                   | Detected at          |
| ------------------------------------------- | -------------------- |
| `TYPE` reserved, or not accepted here       | the `TYPE` byte      |
| `LEN` greater than 8                        | the `LEN` byte       |
| `LEN` not the valid length for `TYPE`       | the `LEN` byte       |
| CRC mismatch                                | the last CRC byte    |
| Reserved field value (§4.5, §4.6)           | the last CRC byte, only once the CRC has matched |

Handling, on both sides:

* A malformed packet SHALL be discarded whole. Nothing in it is acted on, and
  the device sends no reply to it.
* The receiver then looks for the next `SYNC`, starting with the byte after
  the one that made the packet malformed. Bytes received while looking for
  `SYNC` are discarded.
* Any sequence of bytes, of any length, SHALL be processed without undefined
  behaviour and without reading or writing outside the receiver's buffer.
* A device that discards a command sends nothing, so the host's reply timeout
  turns it into `TRANSPORT_FAILURE`.

A corrupted packet can hide the start of the packet after it, if that packet
began inside the corrupted one. The cost is one lost command, which the host
sees as a timeout. This is accepted (ADR-0020, COBS alternative).

## 8. Link health on the host

The host adapter answers `health()` (`REQ-COM-003`) from what it has observed,
without sending anything. When more than one condition holds, the first in
this list wins:

1. **`UNAVAILABLE`** — the port is closed, or no valid `HEARTBEAT` has been
   received in the last 500 ms, including when none has been received since
   the port was opened.
2. **`TRANSPORT_FAILURE`** — the most recent command ended in
   `TRANSPORT_FAILURE` (§6, §7).
3. **`REJECTED`** — the most recent command was answered with `NAK`.
4. **`OK`** — none of the above.

Conditions 2 and 3 are cleared by the next command that is answered with
`ACK`.

| Outcome of `send_*`                                         | `LinkStatus`        |
| ----------------------------------------------------------- | ------------------- |
| Port closed when sending                                    | `UNAVAILABLE`       |
| Matching `ACK` within 50 ms                                 | `OK`                |
| Matching `NAK` within 50 ms                                 | `REJECTED`          |
| No matching reply within 50 ms (nothing, garbage, malformed, wrong `SEQ`) | `TRANSPORT_FAILURE` |

## 9. Device fail-safe

These restate `arduino.instructions.md` and `REQ-SAF-001` / `REQ-SAF-004` as
they apply to the protocol:

* On power-up and reset, the water actuator is inactive before any byte is
  read.
* A burst ends on the device's own timer, at most 500 ms after it starts, even
  if the host never sends another byte. Loss of the serial link during a burst
  therefore cannot leave the water on.
* Malformed input changes nothing (§7).

## 10. Worked examples

All bytes in hex. The CRCs below were computed with the parameters in §2.

`AIM`, `SEQ = 0x07`, x = 12.34° (1234 = `0x04D2`), y = 5.00° (500 = `0x01F4`):

```text
A5 01 07 04 04 D2 01 F4 55 47
```

Its `ACK`:

```text
A5 81 07 00 59 61
```

`FIRE`, `SEQ = 0x08`, 500 ms (`0x01F4`):

```text
A5 02 08 02 01 F4 22 86
```

`HEARTBEAT`, `counter = 42` (`0x002A`), actuator inactive:

```text
A5 83 00 03 00 2A 00 66 21
```

These packets are the first test vectors for `tests/protocol/`.

## 11. Not specified here

* Baud rate (deployment value).
* Whether the host may retry `AIM` or `SAFE` after a timeout. `FIRE` is never
  retried (§4.2).
* The host adapter's serial transport seam (`raspberry/`), and the firmware
  (`arduino/`).
