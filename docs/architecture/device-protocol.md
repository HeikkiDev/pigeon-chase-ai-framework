# Device protocol — Raspberry Pi ↔ Arduino (version 1)

**Status:** specified. The codec is implemented and tested in `protocol/`
(sections 3 to 5 and the vectors of section 10; ADR-0021, follow-up 1), and the
host side behind `ActuatorLink` is implemented and tested against a simulated
device (sections 6 to 8; `raspberry/link/`, ADR-0021, follow-up 2). Not yet
built: a check against a real port and the firmware (section 5.1); the serial
port itself is `SerialPortStream`, tested through a pseudo-terminal. See ADR-0021,
follow-ups 3 and 4.
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
Arduino. A message is **valid** if it passes steps 1–5 of section 5.

## 2. Transport

The protocol runs over any byte stream that may corrupt, drop or duplicate
bytes but does not reorder them. In deployment that is a serial line. The
reference setting is **115200 baud, 8 data bits, no parity, 1 stop bit**; it is
a deployment setting of the transport, not part of the protocol, and the
protocol does not depend on it. Nothing here assumes a particular Arduino
board.

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
  of floating point. The host clamps to its configured envelope first and then
  rounds **towards zero**. A configured bound that is not a whole hundredth can
  therefore be exceeded by at most 0.01°, which is still inside the device's own
  envelope (section 5.1) whenever the configured envelope is.
* Durations are **unsigned 16-bit integers in milliseconds**.

### 3.3 CRC

CRC-16/CCITT-FALSE: polynomial `0x1021`, initial value `0xFFFF`, no input or
output reflection, no final XOR. Check value: the CRC of the ASCII string
`123456789` is `0x29B1`. It covers `body` only.

### 3.4 Size

The largest body is 7 bytes (3 header + 4 payload, for `AIM`). With the 2-byte
CRC that is 9, and COBS adds one: **10 bytes before the delimiter, 11 with it**.

A receiver holds at most **16 bytes before the delimiter**: a round figure
comfortably above the 10-byte maximum and small enough for any microcontroller.
If more than 16 arrive without a
delimiter, the receiver SHALL **discard everything up to and including the next
`0x00`** and parse nothing of what it threw away: the tail of an overlong
message must never be read as the start of a new one. It SHALL NOT buffer
beyond the bound; an unbounded buffer is a way to crash a receiver with noise.

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
| `3`      | `REJECTED_COOLDOWN`     | A `FIRE` arrived while a burst was active or inside the cool-down. Nothing is activated. |
| `4`      | `REJECTED_UNSUPPORTED`  | A message with a valid CRC that the device cannot interpret (section 5). The device did not understand it; it is not a refusal of something understood. |

## 5. Receiving a message

Both sides apply these steps in order to every message, and stop at the first
that fails:

| Step | Check                                                                  | On failure                                                                        |
| ---- | ---------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| 1    | The message fits the 16-byte bound and its COBS encoding is well formed | Discard. No reply.                                                                |
| 2    | At least 5 bytes decode (3 header + 2 CRC), and the CRC matches        | Discard. No reply.                                                                |
| 3    | `version` is 1                                                         | **Device:** reply `REJECTED_UNSUPPORTED`. **Host:** not a valid message; ignored. |
| 4    | `type` is one this side receives                                       | As step 3.                                                                        |
| 5    | The payload length is exactly the length for that type                 | As step 3.                                                                        |
| 6    | *(device only)* the values are within the device's own limits          | Reply with the matching `REJECTED_*` (section 4.3). The command is not carried out. |

Rules that follow from this:

* **Nothing from a failed step 1 or 2 is ever acted on or answered.** A
  corrupt message has no trustworthy `seq`, so no reply could be matched to
  anything. The sender finds out by the reply timeout (section 6).
* An empty message — two delimiters in a row — is ignored.
* The device sends `REJECTED_UNSUPPORTED` with the `seq` and `type` it
  received. The host never replies to anything.
* **A message the host discards or ignores is not an event.** It records no
  fault and counts for nothing: it is simply not evidence that the device is
  there. What the host concludes, it concludes from the absence of valid
  messages (section 7), never from the presence of bad ones.
* A message that passes steps 1–5 is valid, whatever step 6 decides. A valid
  message proves the sender is alive and the line is carrying messages.
* Malformed input is rejected without undefined behaviour: every step reads
  only bytes already received and checked against the bounds above.

### 5.1 Device limits (step 6)

The device enforces its own limits, independently of the host
(`REQ-AIM-002`, `REQ-SAF-001`, `REQ-SAF-005`). The host's clamping is a first
line; this is a second, because a defect on the commanding side must not be
able to move the rig outside its envelope or hold the valve open. The
device's limits SHALL NOT be wider than the deployment values. Its envelope is
fixed in firmware: it is a second line, not a calibration, and it does not
replace the host's configured, initially empty envelope that keeps an
uncalibrated rig inert (`REQ-AIM-002`, `REQ-SAF-004`).

**`AIM`.** X ∈ [−90°, +90°], Y ∈ [0°, +45°]. An out-of-range angle is
refused (`REJECTED_ANGLE`) and the servos **do not move**: failing closed is
chosen over clamping and moving (maintainer ruling, Q26, 2026-11-26). `ACCEPTED` means the device has begun to
move, not that it has arrived. An `AIM` is judged on its angles alone, so it is
accepted during a burst or a cool-down. `FIRE` does not wait for the servos to settle:
the aiming command is sent on an earlier camera frame than the fire command, by
design of the engagement.

**`FIRE`.** The device decides in this order and stops at the first that
applies:

1. `duration_ms` greater than the device's bound (**500 ms**): `REJECTED_DURATION`,
   never truncated. This check comes first so that the reply to a given message
   is always the same.
2. A burst is active, or fewer than **2 s** have elapsed since the last
   non-zero `FIRE` was accepted: `REJECTED_COOLDOWN`. A rejected `FIRE`
   **never starts, restarts or extends** any timer.
3. `duration_ms` is 0: `ACCEPTED`. Nothing is activated and no cool-down starts,
   because nothing was fired (`FireCommand`: a zero duration does nothing).
4. Otherwise: `ACCEPTED`. The actuator is activated, a burst timer for exactly
   `duration_ms` starts, and the cool-down starts **at the moment of
   acceptance**, so that the burst lies inside it
   (`REQ-SAF-005`, ADR-0014: the window is half-open, `elapsed >= 2 s` ends it).

Because a duplicated byte stream can deliver a `FIRE` twice (section 2), and
the device keeps no memory of `seq`, rule 2 is what makes a duplicate harmless:
the second copy arrives during the burst and is refused.

**Burst and cool-down.** An accepted burst ends from the device's own timer
with no further message and no dependence on the host being alive
(`REQ-SAF-001`). `SAFE_STATE` ends a burst at once but does **not** clear the
cool-down; the cool-down is not something a message can shorten. It lives in
the device's volatile memory, so a device that resets forgets it; the host's
own cool-down (`REQ-SAF-005`) is what covers that case.

**Rate.** The 6-per-minute engagement rate is enforced by the host only. The
device enforces the burst bound and the cool-down, which together cap what a
misbehaving host can obtain from it at one bounded burst per cool-down.

**Skew.** The device measures its cool-down from when it accepted the previous
burst, which is later than when the host transmitted it by the latency of the
line. A host that fires exactly at the end of its own cool-down may therefore
be refused by a device that is a few milliseconds behind. That refusal is the
fail-safe outcome and is accepted: it costs one deterrent, never an extra burst.

**`SAFE_STATE`.** The device powers up with the water actuator inactive
(`REQ-SAF-004`), and `SAFE_STATE` deactivates it at once. It is always
`ACCEPTED` and is the cheapest operation the device offers.

## 6. Exchanges

A command and its reply are an **exchange**. The host runs exchanges one at a
time (stop-and-wait): it sends a command and does not send another until the
reply arrives or the reply timeout passes.

* The host picks `seq` for each command, incrementing by one and wrapping.
* A reply answers the outstanding command if and only if its `seq` **and**
  `command_type` match. Any other reply — late, duplicated, or for a command
  already given up on — is **unmatched**: it ends no exchange, records no fault
  or rejection, and clears nothing. It is a valid device message, so it still
  counts as liveness (section 7).
* If no matching reply arrives within the **reply timeout** (100 ms), the
  exchange has faulted.
* **The host SHALL NOT retransmit a command.** A retransmitted `FIRE` whose
  first copy was in fact received would fire twice, and a lost reply is
  indistinguishable from a lost command. Whether anything was sent is not
  knowable from the host's side of the line, which is why a transmitted fire
  command counts whatever the outcome (`REQ-SAF-007`, ADR-0011). Failure is
  reported to the caller, which abandons the engagement (`REQ-COM-002`).
* The device keeps no record of `seq` and does not suppress duplicates. None is
  needed: `AIM` and `SAFE_STATE` are idempotent, and a duplicated `FIRE` is
  refused by the cool-down (section 5.1).

### 6.1 How an exchange ends, and what the call returns

| What the host observes                                                      | Exchange ends | `LinkStatus` returned by the `send_*` call |
| --------------------------------------------------------------------------- | ------------- | ------------------------------------------ |
| A matching `REPLY` with `result = 0`                                        | accepted      | `OK`                                       |
| A matching `REPLY` with `result` 1, 2 or 3                                  | rejected      | `REJECTED`                                 |
| A matching `REPLY` with `result = 4`, or any `result` outside 0–4           | **fault**     | `TRANSPORT_FAILURE`                        |
| A write error; or no matching reply within the reply timeout               | **fault**     | `TRANSPORT_FAILURE`                        |
| The line is not open when the command is due to be sent                     | —             | `UNAVAILABLE` (nothing is transmitted)     |

`REJECTED_UNSUPPORTED` is a fault, not a rejection, because
`LinkStatus::REJECTED` means the device *understood* the command and refused it
(`actuator_link.hpp`); here it did not understand it.

**A command is transmitted whenever the line is open, whatever `health()`
says.** Deciding not to fire on poor health is `core/`'s job (`REQ-SAF-008`),
not the link's, and a link that second-guessed it would make "transmitted" —
which `REQ-SAF-007` counts — depend on state the caller cannot see.
`SAFE_STATE` in particular is attempted whenever the line is open: it is the
operation that must work when nothing else does.

## 7. Liveness and link health

The device sends a `HEARTBEAT` every **500 ms**, from power-up, whether or not
it has been spoken to. The host's picture of the link therefore stays current
without a fire command being sent to refresh it (`REQ-COM-001`,
`REQ-COM-003`).

**Liveness** is the time at which the host last *read a valid device message
from the line*: a heartbeat, a matching reply, or an unmatched reply (section
6). The **link silence timeout** is **1.5 s**, three heartbeat intervals, so one
or two lost heartbeats do not declare the link down and three do. It is judged
by the elapsed-time convention of ADR-0014: silence has lasted the timeout when
`elapsed >= 1.5 s`. Time comes from the injected `MonotonicClock`
(`REQ-DEV-002`), never from the wall clock.

The host's reader SHALL take bytes off the line at least every **100 ms** (it
has to, to see a reply inside the reply timeout) and stamp each valid message
with the time it took it. A reader that finds more than **200 ms** has passed
since it last ran SHALL discard whatever the line buffered meanwhile instead of
stamping it: a backlog of old heartbeats must not make a stalled host look
healthy. The cost is bounded and fails safe: a discarded reply times out and
faults, and a discarded heartbeat is one the next replaces.

The host keeps two further pieces of state, both set only by exchanges:

* **`faulted`** — set when an exchange ends in a fault; cleared when an
  exchange ends accepted or rejected. A heartbeat does **not** clear it. A
  device whose heartbeat timer runs but whose command handling is wedged must
  not read as healthy merely because it is still heartbeating.
* **`rejected`** — set when an `AIM` or `FIRE` exchange ends rejected; cleared
  when an `AIM` or `FIRE` exchange ends accepted. `SAFE_STATE` neither sets nor
  clears it: it is always accepted by a working device and the host sends it
  whenever an engagement is abandoned, so counting it would erase every
  rejection the moment it was made.

`health()` is answered from this state alone and transmits nothing
(`REQ-COM-003`). It returns the first of these that holds:

| Order | `LinkStatus`        | Holds when                                                                                                                                   |
| ----- | ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| 1     | `UNAVAILABLE`       | The line is not open; or no valid device message has been read since it opened; or the last one is at least the silence timeout old.          |
| 2     | `TRANSPORT_FAILURE` | `faulted` is set.                                                                                                                             |
| 3     | `REJECTED`          | `rejected` is set.                                                                                                                            |
| 4     | `OK`                | None of the above.                                                                                                                            |

Consequences worth stating:

* A line that has just opened is `UNAVAILABLE`, not `OK`: nothing is known
  about the device until it has been heard. A host cannot fire into silence.
* A corrupted heartbeat is simply not counted. A corrupted reply is a lost
  reply: its exchange times out and faults.
* `faulted` and `rejected` are cleared differently on purpose. `faulted` means
  "commands are not being answered", and any answer, including to `SAFE_STATE`,
  disproves it. `rejected` means "the device refuses what it is asked to do",
  and `SAFE_STATE`, which a working device never refuses, cannot disprove that.
* An `AIM` is exchanged before any `FIRE`, so on a real link the exchange that
  precedes a fire decision has just cleared or confirmed these two flags. What
  most often refuses a burst on a real link is therefore `UNAVAILABLE`;
  `TRANSPORT_FAILURE` and `REJECTED` are mostly seen between an exchange and the
  next, and are reached at the decision through simulated links, as
  `REQ-SAF-008` and `REQ-COM-003` test them. That is inherent in sending the aim
  first, not a gap.
* **Health can read `OK` for up to about 1.7 s after the device stops**: the
  silence timeout, plus at most 200 ms of reader latency, plus a few
  milliseconds of serial buffering. That is the price of tolerating two lost
  heartbeats, and it is a parameter. It harms nothing: a device that has
  stopped fires nothing, and a command sent into it times out and faults.
* The `actuator_active` heartbeat flag is for the host's diagnostics and its
  logs. It does not enter `health()`.

Something has to read the line for health to move between commands: the host
implementation consumes incoming bytes independently of sending. How it does so
is the implementation's business, not the protocol's.

## 8. Sequences

**Startup.** The device powers up with the actuator inactive and begins
heartbeating. The host opens the line (health `UNAVAILABLE`) and waits for the
first valid device message: a board that resets when the port opens is not
listening yet, and a command sent then would fault. Once it has heard the
device, the host calls `send_safe_state_command` and repeats it, as a new
command with a new `seq` each time, once per heartbeat interval until one is
accepted. Health is `OK` from that exchange. If no heartbeat ever arrives, health
stays `UNAVAILABLE` and nothing is sent; the device is safe regardless
(`REQ-SAF-004`).

**An engagement.** `AIM` (reply `ACCEPTED`) for each aiming frame; then, at the
firing frame, `health()` is consulted and, if `OK`, `FIRE` is sent
(`REQ-SAF-002`, `REQ-SAF-008`). The reply returns when the burst begins; the
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
| Reply timeout               | 100 ms   | Host-side. Under one camera frame period at 5 FPS (`REQ-DET-003`). |
| Host reader period          | ≤ 100 ms | Stall bound 200 ms: later input is discarded, not stamped (section 7). |
| Receive bound               | 16 bytes | Before the delimiter; more is discarded to the next delimiter. |
| Serial line setting         | 115200 8N1 | Deployment setting of the transport, not of the protocol. |

They are listed with the other tunable values in the requirements document.

## 10. Test vectors

These are **normative**: an implementation that does not reproduce them is not
this protocol. They were produced by a reference implementation whose COBS
encoder was first checked against the published COBS examples and whose CRC
against the check value in section 3.3, and every byte was recomputed
independently by a second implementation during review. Bytes are hexadecimal;
each message ends with the `00` delimiter.

### 10.1 Messages that must encode and decode exactly

| #  | Message                                              | Body ‖ CRC (before COBS)      | On the wire                              |
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
| N6 | `11` × 17, then `00`                               | 17 bytes before the delimiter: over the 16-byte bound     | Discard through the delimiter. No reply.                      |
| N7 | `05 11 22 00`                                      | COBS code byte promises more bytes than the message holds | Discard. No reply.                                            |
| N8 | `00`                                               | A lone delimiter: an empty message                        | Ignore.                                                       |
| N9 | `08 01 02 08 F5 01 81 03 00`                       | `FIRE` seq 8, 501 ms — well formed, over the device bound | Reply V6; the actuator is not activated.                      |

| #  | The reply to it                                    | On the wire                              |
| -- | -------------------------------------------------- | ---------------------------------------- |
| R2 | `REPLY` seq 8, `FIRE`, `REJECTED_UNSUPPORTED`      | `08 01 81 08 02 04 96 9F 00`             |
| R3 | `REPLY` seq 3, type `7F`, `REJECTED_UNSUPPORTED`   | `08 01 81 03 7F 04 62 11 00`             |
| R4 | `REPLY` seq 4, `SAFE_STATE`, `REJECTED_UNSUPPORTED` | `08 01 81 04 03 04 C6 D9 00`            |
| R5 | `REPLY` seq 5, `AIM`, `REJECTED_UNSUPPORTED`       | `08 01 81 05 01 04 94 88 00`             |

The same inputs, received by the **host** in place of the device, must be
discarded (N1, N6, N7), ignored (N8), or found not valid and ignored (N2–N5,
which a host would receive only from a broken or incompatible device). In no
case does the host reply, and none of them counts as liveness or as a fault.

## 11. Versioning

`version` is carried in every message so that a mismatch is detected on the
first message that fits the receive bound, rather than inferred from garbled
behaviour. There is no negotiation. A device that does not speak the version
refuses it (section 5); a host that does not speak it ignores every heartbeat,
so it never hears a valid device message, health stays `UNAVAILABLE` and nothing
fires.

**Any change to a message layout, a result code's meaning or a timing default is
a new version.** Adding a new type is a new version too, so that an old receiver
cannot meet a type it half-understands. A new version must keep `version` as the
first byte of the body and keep every message within the receive bound; changing
that bound cannot be negotiated and means updating both devices together.

## 12. Requirement coverage

| Requirement   | Where it is met                                                                               |
| ------------- | --------------------------------------------------------------------------------------------- |
| `REQ-COM-001` | This document; encode/decode tests need no serial device (sections 3, 10); malformed input is rejected (section 5); every command is answered with acceptance or refusal (4.3, 6) and the device heartbeats (7). |
| `REQ-COM-002` | A silent, failing or closed line yields `UNAVAILABLE` or `TRANSPORT_FAILURE` (6.1, 7); the host abandons the engagement. |
| `REQ-COM-003` | `health()` is derived from received messages, exchange outcomes and the clock alone, never from an exchange begun to answer it (7). |
| `REQ-AIM-002` | The device re-checks angles against its own envelope (5.1).                                   |
| `REQ-SAF-001` | The device bounds and self-terminates every burst, and no message can extend one (5.1).       |
| `REQ-SAF-004` | Power-up and `SAFE_STATE` leave the actuator inactive (5.1, 8).                               |
| `REQ-SAF-005` | The device enforces its own cool-down from the moment of acceptance (5.1).                    |
| `REQ-SAF-007` | A transmitted `FIRE` is never retransmitted, and is transmitted whenever the line is open (6, 6.1). |
| `REQ-SAF-008` | Each non-`OK` status has a cause that can occur on a real link (6.1, 7).                      |

## 13. Not specified here

* The serial port implementation, and the host's reader and timers
  (`raspberry/`).
* The firmware (`arduino/`), including which board it runs on and whether it
  uses the microcontroller's hardware watchdog, which would be independent of
  firmware faults in a way a software timer is not.
* The codec's source layout (ADR-0021 plans a shared, header-only module).
* A host-silence watchdog on the device. It was considered and not adopted: see
  ADR-0021.
