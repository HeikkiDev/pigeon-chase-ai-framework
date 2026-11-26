#pragma once

// Verifies: REQ-DEV-001, REQ-DEV-002 (support code — a device on the far end of
// the line, built from the real codec, whose only clock is the test's own), used
// by REQ-COM-001, REQ-COM-002, REQ-COM-003, REQ-SAF-007.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
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
/// It models the *line and the device's answers*, not the device's limits: how
/// a real device decides is `device-protocol.md` section 5.1 and belongs to the
/// firmware. Faults are switched on by the test that needs them.
class SimulatedDevice final : public pigeon::raspberry::ByteStream {
 public:
  /// One command the device received intact.
  struct Command {
    std::uint8_t type{0};
    std::uint8_t seq{0};
  };

  explicit SimulatedDevice(ManualClock& clock) : clock_{clock} {}

  // --- the ByteStream the link sees ---------------------------------------

  [[nodiscard]] bool is_open() const noexcept override { return open_; }

  [[nodiscard]] std::size_t read(std::span<std::uint8_t> out) noexcept override {
    std::size_t count = 0;
    while (count < out.size() && !outbox_.empty()) {
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
    if (outbox_.empty()) {
      clock_.advance_by(max);
    }
  }

  // --- what a test can switch on ------------------------------------------

  void close() noexcept { open_ = false; }
  void fail_writes() noexcept { fail_writes_ = true; }
  /// Receive commands but never answer them.
  void stay_silent() noexcept { silent_ = true; }
  /// Answer with `seq` shifted by `offset`, as a late or stray reply would be.
  void answer_with_seq_offset(std::uint8_t offset) noexcept { seq_offset_ = offset; }
  /// Answer naming a different command type than the one received.
  void answer_naming_type(std::uint8_t type) noexcept { named_type_ = type; }
  /// Answer with this raw result byte instead of `ACCEPTED`.
  void answer_with_result(std::uint8_t result) noexcept { result_ = result; }

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
    commands_.push_back(Command{received.type, received.seq});
    if (silent_) {
      return;
    }

    std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> wire{};
    const auto length = pigeon::protocol::encode_reply(
        static_cast<std::uint8_t>(received.seq + seq_offset_),
        pigeon::protocol::ReplyPayload{named_type_.value_or(received.type), result_},
        pigeon::protocol::MutableBytes{wire.data(), wire.size()});
    for (std::size_t i = 0; i < length; ++i) {
      outbox_.push_back(wire[i]);
    }
  }

  ManualClock& clock_;
  pigeon::protocol::MessageFramer framer_;
  std::deque<std::uint8_t> outbox_;
  std::vector<Command> commands_;
  std::size_t write_calls_{0};
  bool open_{true};
  bool fail_writes_{false};
  bool silent_{false};
  std::uint8_t seq_offset_{0};
  std::optional<std::uint8_t> named_type_;
  std::uint8_t result_{pigeon::protocol::to_byte(pigeon::protocol::ResultCode::ACCEPTED)};
};

}  // namespace pigeon::test_support
