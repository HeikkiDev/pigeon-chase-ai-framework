#pragma once

// Verifies: REQ-SAF-001, REQ-SAF-004, REQ-COM-002, REQ-COM-003, REQ-SAF-008
// (support code — the simulated actuator system the acceptance criteria of
// those requirements are written against: "a simulated actuator", "a simulated
// link that fails after locking", "a simulated link reporting it, before any
// command is sent", "a simulated link that counts transmissions").

#include <chrono>
#include <vector>

#include "pigeon/core/actuator_link.hpp"
#include "support/manual_clock.hpp"

namespace pigeon::test_support {

/// A simulated actuator system: two servos and a water valve, with no hardware
/// behind them.
///
/// It models the obligations `actuator_link.hpp` places on *every*
/// implementation, firmware included:
///
/// * inactive on construction and after a safe-state command (`REQ-SAF-004`);
/// * a burst that self-terminates on the device's own timer, with no further
///   command and no dependence on the caller still being alive
///   (`REQ-SAF-001`);
/// * its own independent bound on the burst length — a longer request is
///   refused, never obeyed (`REQ-SAF-001`);
/// * failure reported as a `LinkStatus`, never thrown (`REQ-COM-002`);
/// * current health answerable at any time, before anything has been sent,
///   without an exchange (`REQ-COM-003`).
///
/// One field answers both questions the interface asks — what an exchange
/// returns, and what `health()` reports — because they are the same condition
/// seen at two moments, and two fields would be two versions of it. Setting it
/// is how a test makes the link unhealthy *before* any command goes out, which
/// is what `REQ-SAF-008` needs in order to be reachable at all (ADR-0016).
///
/// `firmware_burst_bound` is a property of *this double*, standing in for the
/// Arduino's own timer. The requirement fixes no number for it — it says only
/// that the device applies a bound of its own — so the tests assert the
/// relationship (a request above the bound is refused and opens no valve),
/// never the value.
class SimulatedActuatorLink final : public pigeon::core::ActuatorLink {
 public:
  /// The device's own maximum burst. Independent of `SafetyLimits`, which is
  /// the *commanding* side's bound.
  static constexpr std::chrono::milliseconds firmware_burst_bound{750};

  explicit SimulatedActuatorLink(const ManualClock& clock) : clock_{&clock} {}

  /// Set the link's condition: what `health()` reports from now on, and what
  /// the next exchange returns. The `REQ-COM-002` trigger, and the way
  /// `REQ-SAF-008`'s statuses are made to occur before anything is sent
  /// (`REQ-COM-003`).
  void set_status(pigeon::core::LinkStatus status) noexcept { status_ = status; }

  [[nodiscard]] pigeon::core::LinkStatus send_aiming_command(
      const pigeon::core::AimingCommand& command) override {
    ++transmissions_;
    aiming_commands_.push_back(command);
    return status_;
  }

  [[nodiscard]] pigeon::core::LinkStatus send_fire_command(
      const pigeon::core::FireCommand& command) override {
    ++transmissions_;
    fire_commands_.push_back(command);
    if (status_ != pigeon::core::LinkStatus::OK) {
      return status_;
    }
    // The receiving side does not trust the sending side (`REQ-SAF-001`).
    if (command.duration > firmware_burst_bound) {
      return pigeon::core::LinkStatus::REJECTED;
    }
    burst_ends_at_ = clock_->now().since_epoch + command.duration;
    return pigeon::core::LinkStatus::OK;
  }

  [[nodiscard]] pigeon::core::LinkStatus send_safe_state_command() override {
    ++transmissions_;
    ++safe_state_commands_;
    burst_ends_at_ = std::chrono::milliseconds{0};
    return status_;
  }

  /// The link's condition right now (`REQ-COM-003`).
  ///
  /// Answered entirely from state this side already holds: it sends nothing,
  /// increments no transmission count, blocks on nothing and changes nothing.
  /// `transmissions()` is what proves that, and would catch an implementation
  /// that tried to probe the device to answer.
  [[nodiscard]] pigeon::core::LinkStatus health() const noexcept override { return status_; }

  /// How many times anything has been put on the wire (`REQ-COM-003`).
  ///
  /// Counts every `send_*` call, whatever came back. A health query must leave
  /// this untouched.
  [[nodiscard]] int transmissions() const noexcept { return transmissions_; }

  /// Whether the valve is open *now*, according to the device's own timer.
  ///
  /// Deliberately independent of any further command: once the burst has been
  /// started it ends on time even if nobody ever calls anything again
  /// (`REQ-SAF-001`).
  [[nodiscard]] bool water_active() const noexcept {
    return clock_->now().since_epoch < burst_ends_at_;
  }

  [[nodiscard]] const std::vector<pigeon::core::AimingCommand>& aiming_commands() const noexcept {
    return aiming_commands_;
  }

  [[nodiscard]] const std::vector<pigeon::core::FireCommand>& fire_commands() const noexcept {
    return fire_commands_;
  }

  [[nodiscard]] int safe_state_commands() const noexcept { return safe_state_commands_; }

 private:
  // Non-owning observer: the clock outlives the link (cpp.instructions.md).
  const ManualClock* clock_{nullptr};
  pigeon::core::LinkStatus status_{pigeon::core::LinkStatus::OK};
  // Zero means "never fired": the valve is shut at time zero and stays shut.
  std::chrono::milliseconds burst_ends_at_{0};
  std::vector<pigeon::core::AimingCommand> aiming_commands_;
  std::vector<pigeon::core::FireCommand> fire_commands_;
  int safe_state_commands_{0};
  int transmissions_{0};
};

}  // namespace pigeon::test_support
