#include "pigeon/raspberry/serial_actuator_link.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
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

/// An angle in hundredths of a degree, rounded towards zero (section 3.2).
///
/// The host clamps to its envelope first, so an angle that cannot be represented
/// does not arise in an engagement. If one does arrive (not a number, or beyond
/// 327 degrees) it is sent as the nearest value that cannot be represented,
/// which lies outside the device's envelope, so the device refuses it: failing
/// closed rather than guessing a position.
std::int16_t centidegrees(double degrees) noexcept {
  const double scaled = degrees * 100.0;
  if (!(scaled > -32768.0)) {  // also true for NaN
    return std::numeric_limits<std::int16_t>::min();
  }
  if (scaled >= 32767.0) {
    return std::numeric_limits<std::int16_t>::max();
  }
  return static_cast<std::int16_t>(scaled);  // truncates towards zero
}

/// A burst length in milliseconds. One that cannot be represented, or is
/// negative, becomes the largest value, which is over the device's bound and
/// is refused: a duration is never silently shortened into one the device would
/// obey.
std::uint16_t duration_ms(std::chrono::milliseconds duration) noexcept {
  constexpr auto largest = std::numeric_limits<std::uint16_t>::max();
  if (duration.count() < 0 || duration.count() > largest) {
    return largest;
  }
  return static_cast<std::uint16_t>(duration.count());
}

}  // namespace

SerialActuatorLink::SerialActuatorLink(ByteStream& line, const pigeon::core::MonotonicClock& clock,
                                       LinkTimings timings)
    : line_{line}, clock_{clock}, timings_{timings}, last_poll_{clock.now().since_epoch} {}

SerialActuatorLink::~SerialActuatorLink() {
  // Attempted whenever the line is open, even if the device was never heard:
  // it is the operation that must work when nothing else does (section 6.1).
  if (line_.is_open()) {
    static_cast<void>(send_safe_state_command());
  }
}

LinkStatus SerialActuatorLink::start_up(std::chrono::milliseconds max_wait) {
  if (!line_.is_open()) {
    return LinkStatus::UNAVAILABLE;
  }
  const auto deadline = clock_.now().since_epoch + max_wait;

  // Nothing is sent until the device has been heard (section 8).
  poll();
  while (!last_heard_.has_value()) {
    const auto now = clock_.now().since_epoch;
    if (now >= deadline) {
      return LinkStatus::UNAVAILABLE;
    }
    wait_for_line(deadline - now);
    poll();
  }

  for (;;) {
    const auto attempted_at = clock_.now().since_epoch;
    const LinkStatus status = send_safe_state_command();
    const auto next_attempt = attempted_at + timings_.heartbeat_interval;
    if (status == LinkStatus::OK || next_attempt >= deadline) {
      return status;
    }
    // Once per heartbeat interval, measured from the previous attempt. Keep
    // reading the line meanwhile, so that liveness stays current.
    for (auto now = clock_.now().since_epoch; now < next_attempt; now = clock_.now().since_epoch) {
      wait_for_line(next_attempt - now);
      poll();
    }
  }
}

LinkStatus SerialActuatorLink::send_aiming_command(const pigeon::core::AimingCommand& command) {
  const std::uint8_t seq = next_seq_++;
  pigeon::protocol::AimPayload payload;
  payload.x_centidegrees = centidegrees(command.angles.x.degrees);
  payload.y_centidegrees = centidegrees(command.angles.y.degrees);
  std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> wire{};
  const std::size_t length =
      pigeon::protocol::encode_aim(seq, payload, MutableBytes{wire.data(), wire.size()});
  return exchange(MessageType::AIM, seq, std::span<const std::uint8_t>{wire.data(), length});
}

LinkStatus SerialActuatorLink::send_fire_command(const pigeon::core::FireCommand& command) {
  const std::uint8_t seq = next_seq_++;
  std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> wire{};
  // Transmitted whenever the line is open, whatever health() says: deciding not
  // to fire on poor health is core's job (REQ-SAF-008), and a link that
  // second-guessed it would make "transmitted", which REQ-SAF-007 counts, depend
  // on state the caller cannot see (section 6.1).
  const std::size_t length = pigeon::protocol::encode_fire(
      seq, pigeon::protocol::FirePayload{duration_ms(command.duration)},
      MutableBytes{wire.data(), wire.size()});
  return exchange(MessageType::FIRE, seq, std::span<const std::uint8_t>{wire.data(), length});
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
    faulted_ = true;
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
    wait_for_line(deadline - now);
  }
  pending_.reset();
  // Any answer, even a refusal, shows commands are being answered; a fault does
  // not (section 7).
  faulted_ = (outcome == LinkStatus::TRANSPORT_FAILURE);
  // Only AIM and FIRE speak for the device's limits. SAFE_STATE is always
  // accepted by a working device and is sent whenever an engagement is
  // abandoned, so counting it would erase every rejection the moment it was made.
  if (type == MessageType::AIM || type == MessageType::FIRE) {
    if (outcome == LinkStatus::OK) {
      rejected_ = false;
    } else if (outcome == LinkStatus::REJECTED) {
      rejected_ = true;
    }
  }
  return outcome;
}

void SerialActuatorLink::wait_for_line(std::chrono::milliseconds remaining) noexcept {
  line_.wait_readable(std::min(remaining, timings_.reader_period));
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
  if (faulted_) {
    return LinkStatus::TRANSPORT_FAILURE;
  }
  if (rejected_) {
    return LinkStatus::REJECTED;
  }
  return LinkStatus::OK;
}

}  // namespace pigeon::raspberry
