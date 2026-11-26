#include "pigeon/raspberry/serial_actuator_link.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/framer.hpp"
#include "pigeon/protocol/message.hpp"

namespace pigeon::raspberry {

using pigeon::core::LinkStatus;
using pigeon::protocol::ByteView;
using pigeon::protocol::FramerEvent;
using pigeon::protocol::MessageType;
using pigeon::protocol::MutableBytes;
using pigeon::protocol::ReceiveStatus;
using pigeon::protocol::ResultCode;
using pigeon::protocol::Side;
using pigeon::protocol::to_byte;

namespace {

/// How an exchange that got a matching reply ends (section 6.1). `REJECTED_*`
/// is a refusal of something understood; `REJECTED_UNSUPPORTED` and any result
/// outside the defined codes are a fault.
LinkStatus status_for(std::uint8_t result) noexcept {
  if (result == to_byte(ResultCode::ACCEPTED)) {
    return LinkStatus::OK;
  }
  if (result == to_byte(ResultCode::REJECTED_ANGLE) ||
      result == to_byte(ResultCode::REJECTED_DURATION) ||
      result == to_byte(ResultCode::REJECTED_COOLDOWN)) {
    return LinkStatus::REJECTED;
  }
  return LinkStatus::TRANSPORT_FAILURE;
}

}  // namespace

SerialActuatorLink::SerialActuatorLink(ByteStream& line, const pigeon::core::MonotonicClock& clock,
                                       LinkTimings timings)
    : line_{line}, clock_{clock}, timings_{timings}, last_poll_{clock.now().since_epoch} {}

// Not built yet: refuse, so that nothing can be fired through a link that does
// not exist. Replaced test-first by the exchanges of section 6.
LinkStatus SerialActuatorLink::send_aiming_command(const pigeon::core::AimingCommand& /*command*/) {
  return LinkStatus::UNAVAILABLE;
}

LinkStatus SerialActuatorLink::send_fire_command(const pigeon::core::FireCommand& /*command*/) {
  return LinkStatus::UNAVAILABLE;
}

LinkStatus SerialActuatorLink::send_safe_state_command() {
  const std::uint8_t seq = next_seq_++;  // wraps at 256, as the sequence number does
  std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> wire{};
  // A buffer of the maximum wire size always holds a SAFE_STATE message.
  const std::size_t length =
      pigeon::protocol::encode_safe_state(seq, MutableBytes{wire.data(), wire.size()});
  return exchange(MessageType::SAFE_STATE, seq, std::span<const std::uint8_t>{wire.data(), length});
}

LinkStatus SerialActuatorLink::exchange(MessageType type, std::uint8_t seq,
                                        std::span<const std::uint8_t> wire) {
  // Nothing is transmitted on a line that is not open (section 6.1).
  if (!line_.is_open()) {
    return LinkStatus::UNAVAILABLE;
  }
  // Bring the reader up to date first, so that a reply is not mistaken for a
  // backlog because nobody polled for a while.
  poll();
  if (!line_.write(wire)) {
    return LinkStatus::TRANSPORT_FAILURE;
  }

  // The command is never retransmitted, whatever happens next (REQ-SAF-007).
  pending_ = Pending{to_byte(type), seq, std::nullopt};
  const auto deadline = clock_.now().since_epoch + timings_.reply_timeout;

  LinkStatus outcome = LinkStatus::TRANSPORT_FAILURE;
  for (;;) {
    poll();
    if (pending_->result.has_value()) {
      outcome = status_for(*pending_->result);
      break;
    }
    const auto now = clock_.now().since_epoch;
    if (now >= deadline) {  // elapsed >= timeout (ADR-0014)
      break;
    }
    line_.wait_readable(deadline - now);
  }
  pending_.reset();
  return outcome;
}

void SerialActuatorLink::poll() noexcept {
  const auto now = clock_.now().since_epoch;
  const bool stalled = now - last_poll_ > timings_.stall_bound;
  last_poll_ = now;
  if (stalled) {
    framer_.reset();  // half a message from before the stall is not the start of one
  }

  std::array<std::uint8_t, 32> chunk{};
  for (;;) {
    const std::size_t count = line_.read(std::span<std::uint8_t>{chunk});
    if (count == 0) {
      return;
    }
    if (stalled) {
      continue;  // drained and dropped: it is old, and old is not evidence
    }
    for (const std::uint8_t byte : std::span<const std::uint8_t>{chunk}.first(count)) {
      if (framer_.push(byte) == FramerEvent::MESSAGE) {
        on_message(framer_.message(), now);
      }
    }
  }
}

void SerialActuatorLink::on_message(ByteView encoded, std::chrono::milliseconds now) noexcept {
  const auto received = pigeon::protocol::receive_message(encoded, Side::HOST);
  // A message the host discards or finds unsupported is no evidence the device
  // is there; a valid one is, whatever it turns out to answer (section 5).
  if (received.status != ReceiveStatus::VALID) {
    return;
  }
  last_heard_ = now;
  if (received.type != to_byte(MessageType::REPLY)) {
    return;
  }
  // A reply answers the outstanding command if and only if its seq and
  // command_type both match; any other is unmatched and ends nothing (section 6).
  if (pending_.has_value() && !pending_->result.has_value() && received.seq == pending_->seq &&
      received.reply.command_type == pending_->type) {
    pending_->result = received.reply.result;
  }
}

LinkStatus SerialActuatorLink::health() const noexcept {
  // A line that has just opened is UNAVAILABLE, not OK: nothing is known about
  // the device until it has been heard (section 7).
  if (!line_.is_open() || !last_heard_.has_value()) {
    return LinkStatus::UNAVAILABLE;
  }
  if (clock_.now().since_epoch - *last_heard_ >= timings_.silence_timeout) {
    return LinkStatus::UNAVAILABLE;
  }
  return LinkStatus::OK;
}

}  // namespace pigeon::raspberry
