#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/clock.hpp"
#include "pigeon/raspberry/byte_stream.hpp"
#include "pigeon/raspberry/serial_actuator_link.hpp"

namespace pigeon::raspberry {

/// The host link, made safe to use from two threads (ADR-0023).
///
/// It owns a `SerialActuatorLink` and serialises everything that touches it,
/// polling, the commands and `start_up`, with one lock. After each it publishes the
/// link's health state into one lock-free atomic word, and `health()` answers from
/// that copy and the time now. So `health()` never waits for an exchange in
/// progress (`REQ-COM-003`); asked mid-exchange, it reports the state from before
/// the exchange.
///
/// `poll_once()` is what a reader thread calls every reader period. The clock must
/// be safe to read from several threads, as `SteadyClock` is.
class ConcurrentActuatorLink final : public pigeon::core::ActuatorLink {
 public:
  /// Sends nothing. `line` and `clock` must outlive the wrapper.
  ConcurrentActuatorLink(ByteStream& line, const pigeon::core::MonotonicClock& clock,
                         LinkTimings timings = {});

  ConcurrentActuatorLink(const ConcurrentActuatorLink&) = delete;
  ConcurrentActuatorLink& operator=(const ConcurrentActuatorLink&) = delete;
  ConcurrentActuatorLink(ConcurrentActuatorLink&&) = delete;
  ConcurrentActuatorLink& operator=(ConcurrentActuatorLink&&) = delete;

  /// The inner link's destructor puts the actuator in its safe state.
  ~ConcurrentActuatorLink() override = default;

  /// `SerialActuatorLink::start_up`, under the lock, with its outcome published.
  [[nodiscard]] pigeon::core::LinkStatus start_up(std::chrono::milliseconds max_wait);

  [[nodiscard]] pigeon::core::LinkStatus send_aiming_command(
      const pigeon::core::AimingCommand& command) override;
  [[nodiscard]] pigeon::core::LinkStatus send_fire_command(
      const pigeon::core::FireCommand& command) override;
  [[nodiscard]] pigeon::core::LinkStatus send_safe_state_command() override;

  /// From the published state and the time now. Takes no lock.
  [[nodiscard]] pigeon::core::LinkStatus health() const noexcept override;

  /// Reads the line once, under the lock, and publishes what it heard.
  void poll_once() noexcept;

 private:
  /// Called with the lock held.
  void publish() noexcept;

  const pigeon::core::MonotonicClock& clock_;
  std::chrono::milliseconds silence_timeout_;
  std::mutex mutex_;
  SerialActuatorLink link_;
  std::atomic<std::uint64_t> published_;
};

}  // namespace pigeon::raspberry
