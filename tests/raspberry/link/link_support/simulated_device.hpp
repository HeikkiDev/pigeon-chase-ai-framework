#pragma once

// Verifies: REQ-DEV-001, REQ-DEV-002 (support code — a device on the far end of
// the line, built from the real codec, whose only clock is the test's own), used
// by REQ-COM-001, REQ-COM-002, REQ-COM-003, REQ-SAF-007.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/framer.hpp"
#include "pigeon/protocol/message.hpp"
#include "pigeon/raspberry/byte_stream.hpp"
#include "support/manual_clock.hpp"

namespace pigeon::test_support {

/// The Arduino as the host link meets it: bytes in, bytes out, and nothing else.
///
/// It speaks the real codec, so a test exercises the encoding as well as the
/// link. Time passes only inside `wait_readable`, by advancing the test's
/// `ManualClock`, so nothing here sleeps and nothing reads the wall clock.
///
/// It models the *line and the device's answers*. Of the device's own limits
/// (`device-protocol.md` section 5.1) it keeps only the two that need no memory:
/// the aiming envelope and the 500 ms burst bound. The cool-down is a test's to
/// ask for with `answer_with_result(3)`; how a real device keeps it belongs to the
/// firmware. Faults are switched on by the test that needs them.
class SimulatedDevice final : public pigeon::raspberry::ByteStream {
 public:
  /// One command the device received intact.
  struct Command {
    std::uint8_t type{0};
    std::uint8_t seq{0};
    /// `AIM` payload, in hundredths of a degree.
    std::int16_t x_centidegrees{0};
    std::int16_t y_centidegrees{0};
    /// `FIRE` payload, in milliseconds.
    std::uint16_t duration_ms{0};
    /// When the device received it, on the test's clock.
    std::chrono::milliseconds received_at{0};
  };

  explicit SimulatedDevice(ManualClock& clock) : clock_{clock} {}

  // --- the ByteStream the link sees ---------------------------------------

  [[nodiscard]] bool is_open() const noexcept override { return open_; }

  [[nodiscard]] std::size_t read(std::span<std::uint8_t> out) noexcept override {
    release_due_replies();
    const std::size_t limit = one_byte_reads_ ? std::min<std::size_t>(out.size(), 1) : out.size();
    std::size_t count = 0;
    while (count < limit && !outbox_.empty()) {
      out[count++] = outbox_.front();
      outbox_.pop_front();
    }
    return count;
  }

  [[nodiscard]] bool write(std::span<const std::uint8_t> bytes) noexcept override {
    ++write_calls_;
    if (!open_ || fail_writes_) {
      return false;
    }
    for (const std::uint8_t byte : bytes) {
      if (framer_.push(byte) == pigeon::protocol::FramerEvent::MESSAGE) {
        handle(framer_.message());
      }
    }
    return true;
  }

  void wait_readable(std::chrono::milliseconds max) noexcept override {
    release_due_replies();
    if (!outbox_.empty()) {
      return;
    }
    const auto now = clock_.now().since_epoch;
    if (!delayed_.empty() && delayed_.front().due <= now + max &&
        (!heartbeat_interval_.has_value() || delayed_.front().due <= next_heartbeat_)) {
      clock_.advance_by(delayed_.front().due - now);
      release_due_replies();
      return;
    }
    if (heartbeat_interval_.has_value() && next_heartbeat_ <= now + max) {
      clock_.advance_by(next_heartbeat_ - now);
      emit_heartbeat();
      next_heartbeat_ += *heartbeat_interval_;
      return;
    }
    clock_.advance_by(max);
  }

  // --- what a test can switch on ------------------------------------------

  void close() noexcept { open_ = false; }
  /// Heartbeat on its own timer from now on, as the real device does from
  /// power-up: the first after one interval, then every interval. They are put on
  /// the line while the link waits (`wait_readable`), which is the only place
  /// time passes.
  void start_heartbeats(std::chrono::milliseconds interval) noexcept {
    heartbeat_interval_ = interval;
    next_heartbeat_ = clock_.now().since_epoch + interval;
  }
  /// Hold every reply back for this long after its command arrived, as a slow
  /// line or a busy device would. The reply is released while the link waits.
  void delay_replies(std::chrono::milliseconds delay) noexcept { reply_delay_ = delay; }
  /// Hand the link one byte per `read`, so that a message arrives in pieces.
  void deliver_one_byte_per_read() noexcept { one_byte_reads_ = true; }
  /// Put a well-formed `REPLY` on the line now, whether or not anything is
  /// waiting for it: a late reply, or one for a command the host gave up on.
  void emit_reply(std::uint8_t seq, std::uint8_t command_type, std::uint8_t result) {
    std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> wire{};
    const auto length =
        pigeon::protocol::encode_reply(seq, pigeon::protocol::ReplyPayload{command_type, result},
                                       pigeon::protocol::MutableBytes{wire.data(), wire.size()});
    emit_raw(std::span<const std::uint8_t>{wire.data(), length});
  }
  /// Receive the next `count` commands without answering them, then answer.
  void stay_silent_for(std::size_t count) noexcept { silent_for_ = count; }
  void fail_writes() noexcept { fail_writes_ = true; }
  /// Receive commands but never answer them (or start answering them again).
  void stay_silent(bool silent = true) noexcept { silent_ = silent; }
  /// Damage every reply on the line, so that its CRC no longer matches.
  void corrupt_replies() noexcept { corrupt_replies_ = true; }
  /// Answer with `seq` shifted by `offset`, as a late or stray reply would be.
  void answer_with_seq_offset(std::uint8_t offset) noexcept { seq_offset_ = offset; }
  /// Answer naming a different command type than the one received.
  void answer_naming_type(std::uint8_t type) noexcept { named_type_ = type; }
  /// Answer every command with this raw result byte, whatever the limits say.
  void answer_with_result(std::uint8_t result) noexcept { result_ = result; }

  /// Put a well-formed `HEARTBEAT` on the line now, as the device's own timer
  /// would. The test decides when; the device keeps no schedule of its own.
  void emit_heartbeat(std::uint8_t flags = 0) {
    std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> wire{};
    const auto length = pigeon::protocol::encode_heartbeat(
        heartbeat_seq_++, pigeon::protocol::HeartbeatPayload{flags},
        pigeon::protocol::MutableBytes{wire.data(), wire.size()});
    emit_raw(std::span<const std::uint8_t>{wire.data(), length});
  }

  /// Put these exact bytes on the line, damaged or not.
  void emit_raw(std::span<const std::uint8_t> bytes) {
    for (const std::uint8_t byte : bytes) {
      outbox_.push_back(byte);
    }
  }

  // --- what a test can observe --------------------------------------------

  [[nodiscard]] const std::vector<Command>& commands() const noexcept { return commands_; }
  /// How many times the link called `write`, successful or not.
  [[nodiscard]] std::size_t write_calls() const noexcept { return write_calls_; }

 private:
  void handle(pigeon::protocol::ByteView encoded) noexcept {
    const auto received =
        pigeon::protocol::receive_message(encoded, pigeon::protocol::Side::DEVICE);
    if (received.status != pigeon::protocol::ReceiveStatus::VALID) {
      return;  // a device discards what it cannot trust; nothing here needs more
    }
    Command command{received.type, received.seq};
    command.received_at = clock_.now().since_epoch;
    if (received.type == pigeon::protocol::to_byte(pigeon::protocol::MessageType::AIM)) {
      command.x_centidegrees = received.aim.x_centidegrees;
      command.y_centidegrees = received.aim.y_centidegrees;
    } else if (received.type == pigeon::protocol::to_byte(pigeon::protocol::MessageType::FIRE)) {
      command.duration_ms = received.fire.duration_ms;
    }
    commands_.push_back(command);
    if (silent_for_ > 0) {
      --silent_for_;
      return;
    }
    if (silent_) {
      return;
    }

    std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> wire{};
    const auto length = pigeon::protocol::encode_reply(
        static_cast<std::uint8_t>(received.seq + seq_offset_),
        pigeon::protocol::ReplyPayload{named_type_.value_or(received.type), result_for(command)},
        pigeon::protocol::MutableBytes{wire.data(), wire.size()});
    auto reply = std::span<std::uint8_t>{wire.data(), length};
    if (corrupt_replies_ && reply.size() >= 3) {
      std::uint8_t& damaged = reply[reply.size() - 2];  // last byte before the delimiter
      damaged = (damaged == 0x55) ? 0xAA : 0x55;
    }
    if (reply_delay_ > std::chrono::milliseconds{0}) {
      Delayed late{clock_.now().since_epoch + reply_delay_, {}};
      late.bytes.assign(reply.begin(), reply.end());
      delayed_.push_back(std::move(late));
    } else {
      emit_raw(reply);
    }
  }

  void release_due_replies() noexcept {
    while (!delayed_.empty() && delayed_.front().due <= clock_.now().since_epoch) {
      emit_raw(std::span<const std::uint8_t>{delayed_.front().bytes});
      delayed_.pop_front();
    }
  }

  /// What a device with the deployment limits says to this command (5.1).
  [[nodiscard]] std::uint8_t result_for(const Command& command) const noexcept {
    using pigeon::protocol::MessageType;
    using pigeon::protocol::ResultCode;
    if (result_.has_value()) {
      return *result_;
    }
    if (command.type == pigeon::protocol::to_byte(MessageType::AIM)) {
      const bool inside = command.x_centidegrees >= -9000 && command.x_centidegrees <= 9000 &&
                          command.y_centidegrees >= 0 && command.y_centidegrees <= 4500;
      return pigeon::protocol::to_byte(inside ? ResultCode::ACCEPTED : ResultCode::REJECTED_ANGLE);
    }
    if (command.type == pigeon::protocol::to_byte(MessageType::FIRE) && command.duration_ms > 500) {
      return pigeon::protocol::to_byte(ResultCode::REJECTED_DURATION);
    }
    return pigeon::protocol::to_byte(ResultCode::ACCEPTED);
  }

  struct Delayed {
    std::chrono::milliseconds due;
    std::vector<std::uint8_t> bytes;
  };

  ManualClock& clock_;
  pigeon::protocol::MessageFramer framer_;
  std::deque<Delayed> delayed_;
  std::chrono::milliseconds reply_delay_{0};
  bool one_byte_reads_{false};
  std::deque<std::uint8_t> outbox_;
  std::vector<Command> commands_;
  std::size_t write_calls_{0};
  std::uint8_t heartbeat_seq_{0};
  bool open_{true};
  bool fail_writes_{false};
  bool silent_{false};
  std::size_t silent_for_{0};
  std::optional<std::chrono::milliseconds> heartbeat_interval_;
  std::chrono::milliseconds next_heartbeat_{0};
  bool corrupt_replies_{false};
  std::uint8_t seq_offset_{0};
  std::optional<std::uint8_t> named_type_;
  std::optional<std::uint8_t> result_;
};

}  // namespace pigeon::test_support
