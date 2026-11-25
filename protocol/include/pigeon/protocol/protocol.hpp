#pragma once

// The Raspberry Pi <-> Arduino wire protocol (`REQ-COM-001`).
//
// Specification: docs/architecture/protocol.md. Design: ADR-0020.
//
// Compiled by both devices, so this header is written for the Arduino Uno's
// toolchain as much as for the host (Q28): C++17, no standard library, no
// allocation, no exceptions, no RTTI, no virtual functions, no floating point,
// and no assumption that `int` is wider than 16 bits.
//
// avr-gcc ships without a C++ standard library, so <cstdint> and <cstddef> do
// not exist there; the C headers do, and are the only ones used.
// NOLINTBEGIN(modernize-deprecated-headers)
#include <stddef.h>
#include <stdint.h>
// NOLINTEND(modernize-deprecated-headers)

namespace pigeon::protocol {

/// First byte of every packet (protocol.md §2).
constexpr uint8_t sync_byte = 0xA5;

/// Largest payload any packet may carry (protocol.md §2).
constexpr size_t max_payload_size = 8;

/// Size of a packet with an empty payload: sync, type, seq, len, two CRC bytes.
constexpr size_t min_packet_size = 6;

/// Size of a packet with the largest payload.
constexpr size_t max_packet_size = min_packet_size + max_payload_size;

// Values both devices must agree on (protocol.md §4.1, §4.2, §6). Defined once
// here so the firmware and the host adapter cannot drift apart. The codec
// itself does not use them: enforcing them is the firmware's and the host
// adapter's job.

/// Longest burst the device will run; a longer `FIRE` is truncated to this
/// (`REQ-SAF-001`, Q31, Q32).
constexpr uint16_t device_fire_bound_ms = 500;

/// The device's own envelope, inclusive, in centidegrees. An `AIM` outside it
/// is refused with `NAK`, never clamped (`REQ-AIM-002`, Q26).
constexpr int16_t envelope_x_min_centidegrees = -9000;
constexpr int16_t envelope_x_max_centidegrees = 9000;
constexpr int16_t envelope_y_min_centidegrees = 0;
constexpr int16_t envelope_y_max_centidegrees = 4500;

/// How often the device sends `HEARTBEAT` (`REQ-COM-003`, Q29).
constexpr uint16_t heartbeat_interval_ms = 100;

/// How long without a `HEARTBEAT` before the host reports `UNAVAILABLE`
/// (`REQ-COM-003`, Q29).
constexpr uint16_t heartbeat_timeout_ms = 500;

/// How long the host waits for `ACK` or `NAK` before `TRANSPORT_FAILURE`
/// (Q30).
constexpr uint16_t reply_timeout_ms = 50;

/// Message types (protocol.md §3). The top bit is the direction: clear for
/// host -> device, set for device -> host. Every other value is reserved.
enum class MessageType : uint8_t {
  AIM = 0x01,
  FIRE = 0x02,
  SAFE = 0x03,
  ACK = 0x81,
  NAK = 0x82,
  HEARTBEAT = 0x83,
};

/// Why the device refused a command (protocol.md §4.5). Every other value is
/// reserved.
enum class NakReason : uint8_t {
  ANGLE_OUT_OF_ENVELOPE = 0x01,
};

/// One decoded or to-be-encoded message.
///
/// A flat value rather than a variant: the Uno has no `std::variant`, and six
/// small messages do not justify a hand-written one. Only the fields that
/// belong to `type` are meaningful; the rest are ignored by `encode` and left
/// at their defaults by `Decoder`.
struct Message {
  MessageType type{MessageType::SAFE};
  /// Sequence number (protocol.md §5). Zero for `HEARTBEAT`.
  uint8_t seq{0};

  /// `AIM`: pan, in centidegrees (protocol.md §4.1, Q27).
  int16_t x_centidegrees{0};
  /// `AIM`: tilt, in centidegrees.
  int16_t y_centidegrees{0};

  /// `FIRE`: requested burst length, in milliseconds (protocol.md §4.2).
  uint16_t duration_ms{0};

  /// `NAK`: why the command was refused (protocol.md §4.5).
  NakReason reason{NakReason::ANGLE_OUT_OF_ENVELOPE};

  /// `HEARTBEAT`: increments by one per heartbeat (protocol.md §4.6).
  uint16_t counter{0};
  /// `HEARTBEAT`: flags bit 0, the water actuator is active.
  bool actuator_active{false};
};

/// The bytes of one encoded packet.
struct EncodedPacket {
  // A C array, not std::array: the Uno has no standard library.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  uint8_t bytes[max_packet_size]{};
  /// Number of valid bytes in `bytes`. Zero if the message could not be
  /// encoded.
  size_t size{0};
};

/// CRC-16/CCITT-FALSE (protocol.md §2): polynomial 0x1021, initial value
/// 0xFFFF, not reflected, no final XOR. The CRC of the ASCII bytes
/// `123456789` is 0x29B1.
///
/// `data` may be null only when `size` is zero.
[[nodiscard]] uint16_t crc16(const uint8_t* data, size_t size) noexcept;

/// Encodes `message` as one packet (protocol.md §2-§4).
///
/// Never produces a packet that a receiver would treat as malformed
/// (protocol.md §7): a reserved `type`, a reserved `NAK` reason, or a
/// `HEARTBEAT` with a non-zero `seq` yields `size == 0`.
[[nodiscard]] EncodedPacket encode(const Message& message) noexcept;

/// Which end of the link a `Decoder` sits at. Each end accepts only the
/// message types sent towards it (protocol.md §7).
enum class Endpoint : uint8_t {
  /// The Arduino: accepts `AIM`, `FIRE` and `SAFE`.
  DEVICE,
  /// The Raspberry Pi: accepts `ACK`, `NAK` and `HEARTBEAT`.
  HOST,
};

/// What one byte pushed into a `Decoder` did (protocol.md §7).
enum class DecodeStatus : uint8_t {
  /// The byte was consumed: part of a packet in progress, or skipped while
  /// looking for `sync_byte`.
  NEED_MORE,
  /// The byte completed a valid packet. `Decoder::message()` holds it.
  PACKET_READY,
  /// `LEN` was greater than `max_payload_size`. Reported at the `LEN` byte.
  DISCARDED_BAD_LENGTH,
  /// `TYPE` was reserved, or is not accepted at this endpoint. Reported at the
  /// `TYPE` byte.
  DISCARDED_UNKNOWN_TYPE,
  /// `LEN` was not the one valid length for `TYPE`. Reported at the `LEN` byte.
  DISCARDED_WRONG_LENGTH,
  /// The CRC did not match. Reported at the last CRC byte.
  DISCARDED_BAD_CRC,
  /// The CRC matched but a field used a reserved value: a `NAK` reason, a
  /// `HEARTBEAT` `seq`, or a `HEARTBEAT` flag bit. Reported at the last CRC
  /// byte.
  DISCARDED_RESERVED_VALUE,
};

/// Incremental, byte-at-a-time packet decoder (ADR-0020 §8, protocol.md §7).
///
/// Holds no more than one packet. Any sequence of bytes, of any length, is
/// accepted without undefined behaviour. After a `DISCARDED_*` status the
/// decoder looks for the next `sync_byte`, starting with the byte after the one
/// that was reported; that byte is not reconsidered.
class Decoder {
 public:
  explicit Decoder(Endpoint endpoint) noexcept;

  /// Consumes one byte.
  [[nodiscard]] DecodeStatus push(uint8_t byte) noexcept;

  /// The most recent packet, valid after `push` returned `PACKET_READY` and
  /// until the next call to `push`.
  [[nodiscard]] const Message& message() const noexcept;

 private:
  // Implementation state: not part of the interface, and free to change without
  // changing any caller.
  enum class State : uint8_t { SYNC, TYPE, SEQ, LEN, PAYLOAD, CRC_HIGH, CRC_LOW };

  Endpoint endpoint_;
  State state_{State::SYNC};
  /// TYPE, SEQ and LEN of the packet in progress.
  uint8_t type_{0};
  uint8_t seq_{0};
  uint8_t length_{0};
  /// The payload received so far: `size_` of `length_` bytes.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  uint8_t payload_[max_payload_size]{};
  size_t size_{0};
  /// CRC of the covered bytes received so far, and the CRC's high byte.
  uint16_t crc_{0};
  uint8_t crc_high_{0};
  Message message_{};

  /// Finishes a packet whose CRC has arrived, at the last CRC byte.
  [[nodiscard]] DecodeStatus finish(uint8_t crc_low) noexcept;
};

}  // namespace pigeon::protocol
