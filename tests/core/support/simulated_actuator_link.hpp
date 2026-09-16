#pragma once

// Verifies: REQ-SAF-001, REQ-SAF-004, REQ-COM-002 (support code — the
// simulated actuator system the acceptance criteria of those requirements are
// written against: "a simulated actuator", "a simulated link that fails after
// locking").

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
/// * failure reported as a `LinkStatus`, never thrown (`REQ-COM-002`).
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

  /// Simulate the link failing — the `REQ-COM-002` trigger.
  void set_status(pigeon::core::LinkStatus status) noexcept { status_ = status; }

  [[nodiscard]] pigeon::core::LinkStatus send_aiming_command(
      const pigeon::core::AimingCommand& command) override {
    aiming_commands_.push_back(command);
    return status_;
  }

  [[nodiscard]] pigeon::core::LinkStatus send_fire_command(
      const pigeon::core::FireCommand& command) override {
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
    ++safe_state_commands_;
    burst_ends_at_ = std::chrono::milliseconds{0};
    return status_;
  }

  [[nodiscard]] bool is_available() const noexcept override {
    return status_ == pigeon::core::LinkStatus::OK;
  }

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
};

}  // namespace pigeon::test_support
