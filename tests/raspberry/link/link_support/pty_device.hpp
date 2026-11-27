#pragma once

// Verifies: REQ-DEV-003, REQ-DEV-001 (support code: a whole device on the far end
// of a pseudo-terminal, built from the real codec, so that the host's real port,
// clock and reader thread are exercised on every run with no hardware; maintainer
// ruling Q27), used by REQ-COM-001, REQ-COM-003, REQ-SAF-004.

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "link_support/pseudo_terminal.hpp"
#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/framer.hpp"
#include "pigeon/protocol/message.hpp"

namespace pigeon::test_support {

/// The Arduino as the host's real port meets it, on a background thread: it
/// heartbeats, and it answers every command it receives with `ACCEPTED`. It keeps
/// the types of the commands it received, in order.
///
/// Real time and a real thread, so a test asserts only outcomes, never how long
/// anything took (REQ-DEV-002); its pacing only sets how soon they happen.
class PtyDevice {
 public:
  explicit PtyDevice(PseudoTerminal& line) : line_{line}, thread_{[this] { run(); }} {}

  PtyDevice(const PtyDevice&) = delete;
  PtyDevice& operator=(const PtyDevice&) = delete;
  PtyDevice(PtyDevice&&) = delete;
  PtyDevice& operator=(PtyDevice&&) = delete;

  ~PtyDevice() { stop(); }

  /// Stops the device and waits for it. Harmless if already stopped.
  void stop() noexcept {
    stopping_ = true;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  /// The type byte of every command received so far, in order.
  [[nodiscard]] std::vector<std::uint8_t> commands() const {
    const std::scoped_lock lock{mutex_};
    return commands_;
  }

 private:
  void run() {
    using pigeon::protocol::MessageType;
    using pigeon::protocol::ResultCode;
    pigeon::protocol::MessageFramer framer;
    std::uint8_t heartbeat_seq = 0;
    auto next_heartbeat = std::chrono::steady_clock::now();

    while (!stopping_) {
      if (std::chrono::steady_clock::now() >= next_heartbeat) {
        send_heartbeat(heartbeat_seq++);
        next_heartbeat += std::chrono::milliseconds{20};
      }
      for (const std::uint8_t byte : line_.receive_now()) {
        if (framer.push(byte) != pigeon::protocol::FramerEvent::MESSAGE) {
          continue;
        }
        const auto received =
            pigeon::protocol::receive_message(framer.message(), pigeon::protocol::Side::DEVICE);
        if (received.status != pigeon::protocol::ReceiveStatus::VALID) {
          continue;
        }
        {
          const std::scoped_lock lock{mutex_};
          commands_.push_back(received.type);
        }
        send_reply(received.seq, received.type);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
  }

  void send_heartbeat(std::uint8_t seq) {
    std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> wire{};
    const auto length = pigeon::protocol::encode_heartbeat(
        seq, pigeon::protocol::HeartbeatPayload{},
        pigeon::protocol::MutableBytes{wire.data(), wire.size()});
    line_.send(std::span<const std::uint8_t>{wire.data(), length});
  }

  void send_reply(std::uint8_t seq, std::uint8_t command_type) {
    std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> wire{};
    const auto length = pigeon::protocol::encode_reply(
        seq,
        pigeon::protocol::ReplyPayload{
            command_type, pigeon::protocol::to_byte(pigeon::protocol::ResultCode::ACCEPTED)},
        pigeon::protocol::MutableBytes{wire.data(), wire.size()});
    line_.send(std::span<const std::uint8_t>{wire.data(), length});
  }

  PseudoTerminal& line_;
  std::atomic<bool> stopping_{false};
  mutable std::mutex mutex_;
  std::vector<std::uint8_t> commands_;
  std::thread thread_;  // last: it starts once everything above exists
};

}  // namespace pigeon::test_support
