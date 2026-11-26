#pragma once

#include <cstddef>
#include <cstdint>

#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/cobs.hpp"
#include "pigeon/protocol/crc16.hpp"

/// The messages of `docs/architecture/device-protocol.md` and the receiving
/// steps of its section 5. That document is the authority: where this file and
/// it disagree, this file is the defect.
///
/// What this layer does not do is decide anything. It encodes what it is told to,
/// and it reports what it received and whether it could be understood. Whether a
/// duration or an angle is acceptable (section 5, step 6), whether a reply
/// matches an outstanding command, and what any of it means for link health
/// belong to the device and to the host link.
namespace pigeon::protocol {

/// The protocol version this codec speaks (`device-protocol.md`, section 3.1).
inline constexpr std::uint8_t protocol_version = 1;

/// Version, type and sequence number.
inline constexpr std::size_t header_bytes = 3;
/// The CRC-16 that follows the payload, little-endian.
inline constexpr std::size_t crc_bytes = 2;
/// The largest payload, that of `AIM`.
inline constexpr std::size_t max_payload_bytes = 4;
/// The largest message before COBS: header, payload and CRC.
inline constexpr std::size_t max_decoded_message_bytes =
    header_bytes + max_payload_bytes + crc_bytes;
/// The largest message on the wire: the COBS encoding of the largest decoded
/// message, and its delimiter (11 bytes, section 3.4).
inline constexpr std::size_t max_wire_message_bytes =
    cobs_max_encoded_size(max_decoded_message_bytes) + 1;
/// What a receiver holds before the delimiter; more is discarded (section 3.4).
inline constexpr std::size_t max_encoded_message_bytes = 16;

/// The delimiter that ends every message on the wire.
inline constexpr std::uint8_t delimiter = 0x00;

/// Message types (`device-protocol.md`, section 4).
enum class MessageType : std::uint8_t {
  AIM = 0x01,
  FIRE = 0x02,
  SAFE_STATE = 0x03,
  REPLY = 0x81,
  HEARTBEAT = 0x82,
};

/// The results a `REPLY` carries (section 4.3).
enum class ResultCode : std::uint8_t {
  ACCEPTED = 0,
  REJECTED_ANGLE = 1,
  REJECTED_DURATION = 2,
  REJECTED_COOLDOWN = 3,
  REJECTED_UNSUPPORTED = 4,
};

[[nodiscard]] constexpr std::uint8_t to_byte(MessageType type) noexcept {
  return static_cast<std::uint8_t>(type);
}

[[nodiscard]] constexpr std::uint8_t to_byte(ResultCode result) noexcept {
  return static_cast<std::uint8_t>(result);
}

/// `AIM`: where to point, in hundredths of a degree in the servo frame.
///
/// A plain aggregate with named fields, on purpose. A constructor taking two
/// angles of the same type is exactly the call in which X and Y are swapped by
/// mistake, which is what clang-tidy warns of; code in this module sets the
/// fields by name instead, and never initialises it positionally (designated
/// initialisers are C++20, which the firmware toolchain may not have).
struct AimPayload {
  std::int16_t x_centidegrees{0};
  std::int16_t y_centidegrees{0};
};

/// `FIRE`: the requested burst length in milliseconds. Zero is a command that
/// does nothing, as `FireCommand` is.
struct FirePayload {
  constexpr FirePayload() noexcept = default;
  explicit constexpr FirePayload(std::uint16_t duration) noexcept : duration_ms(duration) {}

  std::uint16_t duration_ms{0};
};

/// `REPLY`: which command it answers and what became of it. Both are kept as the
/// raw bytes received, so that a result outside `ResultCode` — which the host
/// must treat as a fault, not as something to guess at — is representable.
///
/// A payload that was never filled in carries `no_result`, which is outside the
/// defined codes, and **not** zero: zero is `ACCEPTED`, so a default of zero would
/// make a discarded or unsupported message read as an accepted command to a host
/// that forgot to check the status first.
struct ReplyPayload {
  /// "No result": a value outside every `ResultCode`, which a host treats as a
  /// fault. What an unfilled payload holds.
  static constexpr std::uint8_t no_result = 0xFF;

  constexpr ReplyPayload() noexcept = default;
  /// The raw form, for relaying a type byte that is not a `MessageType` (a device
  /// answering a message it could not interpret echoes whatever type it carried)
  /// and for representing a result that is not a `ResultCode`. Everything else
  /// uses the typed form below, in which the two cannot be swapped.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  constexpr ReplyPayload(std::uint8_t command, std::uint8_t outcome) noexcept
      : command_type(command), result(outcome) {}
  constexpr ReplyPayload(MessageType command, ResultCode outcome) noexcept
      : command_type(to_byte(command)), result(to_byte(outcome)) {}

  std::uint8_t command_type{0};
  std::uint8_t result{no_result};
};

/// `HEARTBEAT`: the device's flags. Bits other than those named here are
/// reserved, sent as zero and ignored by the receiver.
struct HeartbeatPayload {
  /// Bit 0: the water actuator is active now.
  static constexpr std::uint8_t actuator_active = 0x01;

  /// The bits that mean something. The others are reserved, and an encoder sends
  /// them as zero whatever it is given (`device-protocol.md`, section 4.2).
  static constexpr std::uint8_t defined_flags = actuator_active;

  constexpr HeartbeatPayload() noexcept = default;
  explicit constexpr HeartbeatPayload(std::uint8_t device_flags) noexcept : flags(device_flags) {}

  std::uint8_t flags{0};
};

namespace detail {

/// Builds a message body, checksum included, in storage of its own. Every write
/// goes through `MutableBytes`, so a body that does not fit is reported by
/// `finish()` returning zero and can never overrun anything.
class BodyBuilder {
 public:
  constexpr BodyBuilder(MessageType type, std::uint8_t seq) noexcept {
    put(protocol_version);
    put(to_byte(type));
    put(seq);
  }

  constexpr void put(std::uint8_t byte) noexcept {
    ok_ = ok_ && buffer_.writable().set(size_, byte);
    ++size_;
  }

  /// Little-endian, as everywhere in the protocol (section 3.2).
  constexpr void put_u16(std::uint16_t value) noexcept {
    const std::uint32_t wide = value;
    put(static_cast<std::uint8_t>(wide & 0xFFU));
    put(static_cast<std::uint8_t>((wide >> 8U) & 0xFFU));
  }

  /// Appends the CRC, encodes the body into `out` with COBS and writes the
  /// delimiter after it. Returns the number of bytes written, delimiter
  /// included, or zero if `out` is too small.
  [[nodiscard]] constexpr std::size_t finish(MutableBytes out) noexcept {
    const std::uint16_t crc = crc16(buffer_.view(size_));
    put_u16(crc);
    if (!ok_) {
      return 0;
    }
    const CobsResult encoded = cobs_encode(buffer_.view(size_), out);
    if (encoded.status != CobsStatus::OK || !out.set(encoded.length, delimiter)) {
      return 0;
    }
    return encoded.length + 1;
  }

 private:
  FixedBuffer<max_decoded_message_bytes> buffer_;
  std::size_t size_{0};
  bool ok_{true};
};

/// The unsigned 16-bit little-endian integer at `offset`, or zero if it does not
/// lie wholly inside `bytes`.
[[nodiscard]] constexpr std::uint16_t read_u16(ByteView bytes, std::size_t offset) noexcept {
  const std::uint32_t low = bytes.at(offset);
  const std::uint32_t high = bytes.at(offset + 1);
  return static_cast<std::uint16_t>(low | (high << 8U));
}

}  // namespace detail

/// Each `encode_*` writes one whole message — encoded, with its delimiter — to
/// the start of `out` and returns the number of bytes written. It returns zero
/// if `out` is too small, and never writes beyond it. `max_wire_message_bytes`
/// always suffices.
[[nodiscard]] constexpr std::size_t encode_aim(std::uint8_t seq, const AimPayload& aim,
                                               MutableBytes out) noexcept {
  detail::BodyBuilder body{MessageType::AIM, seq};
  body.put_u16(static_cast<std::uint16_t>(aim.x_centidegrees));
  body.put_u16(static_cast<std::uint16_t>(aim.y_centidegrees));
  return body.finish(out);
}

[[nodiscard]] constexpr std::size_t encode_fire(std::uint8_t seq, const FirePayload& fire,
                                                MutableBytes out) noexcept {
  detail::BodyBuilder body{MessageType::FIRE, seq};
  body.put_u16(fire.duration_ms);
  return body.finish(out);
}

[[nodiscard]] constexpr std::size_t encode_safe_state(std::uint8_t seq, MutableBytes out) noexcept {
  detail::BodyBuilder body{MessageType::SAFE_STATE, seq};
  return body.finish(out);
}

[[nodiscard]] constexpr std::size_t encode_reply(std::uint8_t seq, const ReplyPayload& reply,
                                                 MutableBytes out) noexcept {
  detail::BodyBuilder body{MessageType::REPLY, seq};
  body.put(reply.command_type);
  body.put(reply.result);
  return body.finish(out);
}

[[nodiscard]] constexpr std::size_t encode_heartbeat(std::uint8_t seq,
                                                     const HeartbeatPayload& heartbeat,
                                                     MutableBytes out) noexcept {
  detail::BodyBuilder body{MessageType::HEARTBEAT, seq};
  const std::uint32_t flags = heartbeat.flags;
  const std::uint32_t defined = HeartbeatPayload::defined_flags;
  body.put(static_cast<std::uint8_t>(flags & defined));
  return body.finish(out);
}

/// Which end of the line is receiving: the device (the Arduino) or the host (the
/// Raspberry Pi). Each receives different types (section 4).
enum class Side : std::uint8_t { DEVICE, HOST };

/// What `receive_message` made of what it was handed (section 5).
enum class ReceiveStatus : std::uint8_t {
  /// Steps 1 to 5 passed: the message is well formed, from a version this codec
  /// speaks, of a type this side receives, with the right payload length.
  VALID,
  /// Step 1: too long for the receive bound, or not a COBS encoding. Discarded;
  /// no reply; nothing in it is believed.
  DISCARDED_FRAMING,
  /// Step 2: too short to hold a header and a CRC, or the CRC does not match.
  /// Discarded; no reply; nothing in it is believed.
  DISCARDED_CHECK,
  /// Step 3: the CRC is good but the version is not this codec's. The device
  /// answers `REJECTED_UNSUPPORTED`; the host disregards it.
  UNSUPPORTED_VERSION,
  /// Step 4: the CRC is good but the type is not one this side receives.
  UNSUPPORTED_TYPE,
  /// Step 5: the CRC is good but the payload is not the length its type has.
  UNSUPPORTED_LENGTH,
};

/// Whether a device answers a message with `REJECTED_UNSUPPORTED` (steps 3 to
/// 5). Steps 1 and 2 are answered with nothing.
///
/// Only a *device* answers. A host never replies to anything, and a message it
/// finds unsupported is not a valid device message: it is no evidence that the
/// device is alive, however good its CRC (`device-protocol.md`, sections 5 and 7).
[[nodiscard]] constexpr bool is_unsupported(ReceiveStatus status) noexcept {
  return status == ReceiveStatus::UNSUPPORTED_VERSION ||
         status == ReceiveStatus::UNSUPPORTED_TYPE || status == ReceiveStatus::UNSUPPORTED_LENGTH;
}

/// A received message, as far as it could be understood.
///
/// `version`, `type` and `seq` are set whenever the CRC passed, that is for
/// `VALID` and every `UNSUPPORTED_*` status, because a device has to echo the
/// sequence number and type of a message it could not interpret. They are
/// meaningless for `DISCARDED_*`: a message that failed its check has no
/// trustworthy fields.
///
/// Only the payload member that `type` names is set, and only for `VALID`. Every
/// other member holds a default, so reading one without checking `status` first
/// is a bug: the defaults are chosen so that it fails safe (a reply's `result` is
/// `ReplyPayload::no_result`, not `ACCEPTED`), but the right habit is to check.
struct Received {
  ReceiveStatus status{ReceiveStatus::DISCARDED_FRAMING};
  std::uint8_t version{0};
  std::uint8_t type{0};
  std::uint8_t seq{0};

  AimPayload aim;
  FirePayload fire;
  ReplyPayload reply;
  HeartbeatPayload heartbeat;
};

namespace detail {

/// Whether `side` receives messages of this raw type (section 4).
[[nodiscard]] constexpr bool receives(Side side, std::uint8_t type) noexcept {
  if (side == Side::DEVICE) {
    return type == to_byte(MessageType::AIM) || type == to_byte(MessageType::FIRE) ||
           type == to_byte(MessageType::SAFE_STATE);
  }
  return type == to_byte(MessageType::REPLY) || type == to_byte(MessageType::HEARTBEAT);
}

/// The payload length of a type this side receives (section 4).
[[nodiscard]] constexpr std::size_t payload_size(std::uint8_t type) noexcept {
  if (type == to_byte(MessageType::AIM)) {
    return 4;
  }
  if (type == to_byte(MessageType::FIRE) || type == to_byte(MessageType::REPLY)) {
    return 2;
  }
  if (type == to_byte(MessageType::HEARTBEAT)) {
    return 1;
  }
  return 0;  // SAFE_STATE has none
}

/// Reads the payload of a message already known to have the right length for its
/// type. The 16-bit signed values are read as unsigned and converted: two's
/// complement on every target this runs on, and defined from C++20.
constexpr void read_payload(Received& received, ByteView payload) noexcept {
  const std::uint8_t type = received.type;
  if (type == to_byte(MessageType::AIM)) {
    received.aim.x_centidegrees = static_cast<std::int16_t>(read_u16(payload, 0));
    received.aim.y_centidegrees = static_cast<std::int16_t>(read_u16(payload, 2));
  } else if (type == to_byte(MessageType::FIRE)) {
    received.fire = FirePayload{read_u16(payload, 0)};
  } else if (type == to_byte(MessageType::REPLY)) {
    received.reply = ReplyPayload{payload.at(0), payload.at(1)};
  } else if (type == to_byte(MessageType::HEARTBEAT)) {
    received.heartbeat = HeartbeatPayload{payload.at(0)};
  }
}

}  // namespace detail

/// Applies the receiving steps of `device-protocol.md`, section 5, to one
/// message: the bytes found between two delimiters, the delimiters themselves
/// removed.
///
/// Total on every input: any bytes whatever are answered with a status, nothing
/// is read outside `encoded`, and nothing is believed until its CRC has passed.
/// Step 6, the device's own limits on the values, is not applied here; a `FIRE`
/// of 501 ms is `VALID` to the codec and refused by the device.
[[nodiscard]] constexpr Received receive_message(ByteView encoded, Side receiver) noexcept {
  Received received;

  if (encoded.size() > max_encoded_message_bytes) {
    return received;  // DISCARDED_FRAMING
  }
  FixedBuffer<max_encoded_message_bytes> decoded;
  const CobsResult unstuffed = cobs_decode(encoded, decoded.writable());
  if (unstuffed.status != CobsStatus::OK) {
    return received;  // DISCARDED_FRAMING
  }

  const ByteView body = decoded.view(unstuffed.length);
  if (body.size() < header_bytes + crc_bytes) {
    received.status = ReceiveStatus::DISCARDED_CHECK;
    return received;
  }
  const std::size_t covered = body.size() - crc_bytes;
  if (crc16(body.subview(0, covered)) != detail::read_u16(body, covered)) {
    received.status = ReceiveStatus::DISCARDED_CHECK;
    return received;
  }

  // The CRC has passed, so the header can be trusted enough to be echoed.
  received.version = body.at(0);
  received.type = body.at(1);
  received.seq = body.at(2);

  if (received.version != protocol_version) {
    received.status = ReceiveStatus::UNSUPPORTED_VERSION;
    return received;
  }
  if (!detail::receives(receiver, received.type)) {
    received.status = ReceiveStatus::UNSUPPORTED_TYPE;
    return received;
  }
  const std::size_t payload_length = covered - header_bytes;
  if (payload_length != detail::payload_size(received.type)) {
    received.status = ReceiveStatus::UNSUPPORTED_LENGTH;
    return received;
  }

  detail::read_payload(received, body.subview(header_bytes, payload_length));
  received.status = ReceiveStatus::VALID;
  return received;
}

}  // namespace pigeon::protocol
