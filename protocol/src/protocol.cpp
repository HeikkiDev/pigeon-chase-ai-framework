// The Raspberry Pi <-> Arduino wire protocol codec (`REQ-COM-001`).
//
// Specification: docs/architecture/protocol.md. Design: ADR-0020.
//
// Written for the Arduino Uno as much as for the host (Q28): no standard
// library, no allocation, no floating point, and no assumption that `int` is
// wider than 16 bits. Every shift is applied to an unsigned operand that is at
// least 16 bits wide after promotion, and every result is cast back to its
// fixed-width type explicitly.
#include "pigeon/protocol/protocol.hpp"

namespace pigeon::protocol {
namespace {

constexpr uint16_t crc_initial = 0xFFFFU;
constexpr uint16_t crc_polynomial = 0x1021U;
constexpr uint16_t crc_top_bit = 0x8000U;

/// Offset of the payload within a packet (protocol.md §2).
constexpr size_t payload_offset = 4;

/// `HEARTBEAT` flags bit 0: the water actuator is active (protocol.md §4.6).
constexpr uint8_t flag_actuator_active = 0x01U;

/// Folds one byte into a CRC-16/CCITT-FALSE (protocol.md §2).
[[nodiscard]] uint16_t crc_update(uint16_t crc, uint8_t byte) noexcept {
  crc = static_cast<uint16_t>(crc ^ static_cast<uint16_t>(static_cast<uint16_t>(byte) << 8U));
  for (uint8_t bit = 0; bit < 8U; ++bit) {
    const bool top_set = (crc & crc_top_bit) != 0U;
    crc = static_cast<uint16_t>(crc << 1U);
    if (top_set) {
      crc = static_cast<uint16_t>(crc ^ crc_polynomial);
    }
  }
  return crc;
}

/// The one valid payload length of `type` (protocol.md §3). False if `type`
/// is reserved.
[[nodiscard]] bool payload_length(uint8_t type, uint8_t& length) noexcept {
  switch (static_cast<MessageType>(type)) {
    case MessageType::AIM:
      length = 4;
      return true;
    case MessageType::FIRE:
      length = 2;
      return true;
    case MessageType::SAFE:
    case MessageType::ACK:
      length = 0;
      return true;
    case MessageType::NAK:
      length = 1;
      return true;
    case MessageType::HEARTBEAT:
      length = 3;
      return true;
  }
  return false;
}

/// Is `type` one that `endpoint` receives (protocol.md §3, §7)? The top bit of
/// `TYPE` is the direction: clear for host -> device.
[[nodiscard]] bool is_accepted(Endpoint endpoint, uint8_t type) noexcept {
  uint8_t length = 0;
  if (!payload_length(type, length)) {
    return false;
  }
  const bool towards_host = (type & 0x80U) != 0U;
  return towards_host == (endpoint == Endpoint::HOST);
}

[[nodiscard]] uint8_t high_byte(uint16_t value) noexcept {
  return static_cast<uint8_t>(value >> 8U);
}

[[nodiscard]] uint8_t low_byte(uint16_t value) noexcept {
  return static_cast<uint8_t>(value & 0xFFU);
}

[[nodiscard]] uint16_t big_endian(uint8_t high, uint8_t low) noexcept {
  return static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint16_t>(high) << 8U) | low);
}

/// Two's complement reinterpretation (protocol.md §2), without relying on an
/// implementation-defined narrowing conversion.
[[nodiscard]] uint16_t to_unsigned(int16_t value) noexcept {
  return value < 0 ? static_cast<uint16_t>(0xFFFFU - static_cast<uint16_t>(-(value + 1)))
                   : static_cast<uint16_t>(value);
}

[[nodiscard]] int16_t to_signed(uint16_t value) noexcept {
  return value > 0x7FFFU ? static_cast<int16_t>(-static_cast<int16_t>(0xFFFFU - value) - 1)
                         : static_cast<int16_t>(value);
}

}  // namespace

uint16_t crc16(const uint8_t* data, size_t size) noexcept {
  uint16_t crc = crc_initial;
  for (size_t index = 0; index < size; ++index) {
    // The caller's buffer arrives as a pointer and a size: the Uno has no span.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    crc = crc_update(crc, data[index]);
  }
  return crc;
}

EncodedPacket encode(const Message& message) noexcept {
  EncodedPacket packet{};
  const auto type = static_cast<uint8_t>(message.type);
  uint8_t length = 0;
  if (!payload_length(type, length)) {
    return packet;
  }

  packet.bytes[0] = sync_byte;
  packet.bytes[1] = type;
  packet.bytes[2] = message.seq;
  packet.bytes[3] = length;

  switch (message.type) {
    case MessageType::AIM: {
      const uint16_t x_value = to_unsigned(message.x_centidegrees);
      const uint16_t y_value = to_unsigned(message.y_centidegrees);
      packet.bytes[payload_offset + 0] = high_byte(x_value);
      packet.bytes[payload_offset + 1] = low_byte(x_value);
      packet.bytes[payload_offset + 2] = high_byte(y_value);
      packet.bytes[payload_offset + 3] = low_byte(y_value);
      break;
    }
    case MessageType::FIRE:
      packet.bytes[payload_offset + 0] = high_byte(message.duration_ms);
      packet.bytes[payload_offset + 1] = low_byte(message.duration_ms);
      break;
    case MessageType::NAK:
      if (message.reason != NakReason::ANGLE_OUT_OF_ENVELOPE) {
        return EncodedPacket{};
      }
      packet.bytes[payload_offset] = static_cast<uint8_t>(message.reason);
      break;
    case MessageType::HEARTBEAT:
      if (message.seq != 0U) {
        return EncodedPacket{};
      }
      packet.bytes[payload_offset + 0] = high_byte(message.counter);
      packet.bytes[payload_offset + 1] = low_byte(message.counter);
      packet.bytes[payload_offset + 2] = message.actuator_active ? flag_actuator_active : 0U;
      break;
    case MessageType::SAFE:
    case MessageType::ACK:
      break;
  }

  const size_t crc_offset = payload_offset + length;
  const uint16_t crc = crc16(&packet.bytes[1], crc_offset - 1);
  // crc_offset + 1 < max_packet_size: `length` comes from payload_length.
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
  packet.bytes[crc_offset] = high_byte(crc);
  packet.bytes[crc_offset + 1] = low_byte(crc);
  // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
  packet.size = crc_offset + 2;
  return packet;
}

Decoder::Decoder(Endpoint endpoint) noexcept : endpoint_{endpoint} {}

DecodeStatus Decoder::push(uint8_t byte) noexcept {
  switch (state_) {
    case State::SYNC:
      if (byte == sync_byte) {
        crc_ = crc_initial;
        state_ = State::TYPE;
      }
      return DecodeStatus::NEED_MORE;

    case State::TYPE:
      if (!is_accepted(endpoint_, byte)) {
        state_ = State::SYNC;
        return DecodeStatus::DISCARDED_UNKNOWN_TYPE;
      }
      type_ = byte;
      crc_ = crc_update(crc_, byte);
      state_ = State::SEQ;
      return DecodeStatus::NEED_MORE;

    case State::SEQ:
      seq_ = byte;
      crc_ = crc_update(crc_, byte);
      state_ = State::LEN;
      return DecodeStatus::NEED_MORE;

    case State::LEN: {
      if (byte > max_payload_size) {
        state_ = State::SYNC;
        return DecodeStatus::DISCARDED_BAD_LENGTH;
      }
      uint8_t expected = 0;
      if (!payload_length(type_, expected) || byte != expected) {
        state_ = State::SYNC;
        return DecodeStatus::DISCARDED_WRONG_LENGTH;
      }
      length_ = byte;
      size_ = 0;
      crc_ = crc_update(crc_, byte);
      state_ = length_ == 0U ? State::CRC_HIGH : State::PAYLOAD;
      return DecodeStatus::NEED_MORE;
    }

    case State::PAYLOAD:
      if (size_ < max_payload_size) {
        // Bounded by the check above; `length_` is at most max_payload_size.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        payload_[size_] = byte;
        ++size_;
      }
      crc_ = crc_update(crc_, byte);
      if (size_ >= length_) {
        state_ = State::CRC_HIGH;
      }
      return DecodeStatus::NEED_MORE;

    case State::CRC_HIGH:
      crc_high_ = byte;
      state_ = State::CRC_LOW;
      return DecodeStatus::NEED_MORE;

    case State::CRC_LOW:
      state_ = State::SYNC;
      return finish(byte);
  }

  state_ = State::SYNC;
  return DecodeStatus::NEED_MORE;
}

DecodeStatus Decoder::finish(uint8_t crc_low) noexcept {
  if (big_endian(crc_high_, crc_low) != crc_) {
    return DecodeStatus::DISCARDED_BAD_CRC;
  }

  Message decoded{};
  decoded.type = static_cast<MessageType>(type_);
  decoded.seq = seq_;
  switch (decoded.type) {
    case MessageType::AIM:
      decoded.x_centidegrees = to_signed(big_endian(payload_[0], payload_[1]));
      decoded.y_centidegrees = to_signed(big_endian(payload_[2], payload_[3]));
      break;
    case MessageType::FIRE:
      decoded.duration_ms = big_endian(payload_[0], payload_[1]);
      break;
    case MessageType::NAK:
      if (payload_[0] != static_cast<uint8_t>(NakReason::ANGLE_OUT_OF_ENVELOPE)) {
        return DecodeStatus::DISCARDED_RESERVED_VALUE;
      }
      decoded.reason = NakReason::ANGLE_OUT_OF_ENVELOPE;
      break;
    case MessageType::HEARTBEAT:
      if (seq_ != 0U || (payload_[2] & static_cast<uint8_t>(~flag_actuator_active)) != 0U) {
        return DecodeStatus::DISCARDED_RESERVED_VALUE;
      }
      decoded.counter = big_endian(payload_[0], payload_[1]);
      decoded.actuator_active = (payload_[2] & flag_actuator_active) != 0U;
      break;
    case MessageType::SAFE:
    case MessageType::ACK:
      break;
  }

  message_ = decoded;
  return DecodeStatus::PACKET_READY;
}

const Message& Decoder::message() const noexcept { return message_; }

}  // namespace pigeon::protocol
