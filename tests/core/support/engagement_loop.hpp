#pragma once

// Verifies: support code for REQ-COM-002, REQ-SAF-002, REQ-SAF-005,
// REQ-SAF-007, REQ-SAF-008, REQ-TRK-011, REQ-TRK-012 — the application loop
// the scenario tests drive.
//
// This is the wiring `raspberry/` will eventually own: associate, advance,
// authorise, transmit. It lives in tests/ because no such loop exists yet and
// the scenario requirements are statements about the *composition* of the
// core components, not about any one of them.

#include <optional>
#include <utility>
#include <vector>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/aiming.hpp"
#include "pigeon/core/clock.hpp"
#include "pigeon/core/configuration.hpp"
#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/core/safety_policy.hpp"
#include "pigeon/core/target_state.hpp"
#include "pigeon/core/track.hpp"

namespace pigeon::test_support {

/// Everything observable about one frame's pass through the system.
struct FrameOutcome {
  pigeon::core::TargetState state_before{pigeon::core::TargetState::SEARCHING};
  pigeon::core::TargetState state_after{pigeon::core::TargetState::SEARCHING};
  pigeon::core::EngagementIntent intent{pigeon::core::EngagementIntent::KEEP_SEARCHING};
  bool aiming_command_sent{false};
  bool fire_command_sent{false};
  bool engagement_abandoned{false};
  std::optional<pigeon::core::FireRefusal> refusal;
  std::optional<pigeon::core::ServoAngles> commanded_aim;
  std::optional<pigeon::core::LinkStatus> fire_status;
  /// The link health presented to `authorise_fire` (`REQ-SAF-008`), recorded so
  /// a scenario can assert which status actually reached the policy rather than
  /// assuming it.
  std::optional<pigeon::core::LinkStatus> link_status_presented;
  std::vector<pigeon::core::Track> tracks_after;
};

/// One frame in, one decision out.
class EngagementLoop {
 public:
  EngagementLoop(pigeon::core::Configuration configuration,
                 const pigeon::core::MonotonicClock& clock, pigeon::core::ActuatorLink& link,
                 pigeon::core::ImageSize image_size)
      : configuration_{configuration},
        policy_{configuration, clock},
        link_{&link},
        image_size_{image_size} {}

  /// What the application currently believes about the link's health
  /// (`REQ-SAF-008`).
  ///
  /// `ActuatorLink` offers no way to ask the device for a detailed status
  /// before sending it something: `is_available()` is a bool hint, and every
  /// richer answer arrives as the `LinkStatus` returned by an exchange that has
  /// already happened. So an application's knowledge of link health is exactly
  /// the outcome of its most recent exchange, and that is what this loop
  /// presents to `authorise_fire`.
  ///
  /// Tests that need a specific status at the moment of the fire decision — a
  /// transport failure appearing between the aiming frame and the firing frame,
  /// say — set it here. Passing `std::nullopt` returns the loop to deriving the
  /// status from the link itself.
  void set_observed_link_status(std::optional<pigeon::core::LinkStatus> status) noexcept {
    observed_link_status_ = status;
  }

  /// Process one frame's detections.
  FrameOutcome process(const pigeon::core::DetectionOutcome& outcome) {
    FrameOutcome result;
    result.state_before = state_.state;

    pigeon::core::TrackUpdate update = pigeon::core::associate_detections(
        state_.tracks, outcome, configuration_.association_radius, state_.next_id);
    const pigeon::core::FrameInput input =
        update.tracks.empty() ? pigeon::core::FrameInput::none()
                              : pigeon::core::FrameInput::found(std::move(update));

    const pigeon::core::TargetTransition transition = pigeon::core::advance(state_, input);
    result.intent = transition.intent;
    state_ = transition.next;

    switch (transition.intent) {
      case pigeon::core::EngagementIntent::AIM_AT_TARGET:
        aim(transition, result);
        break;
      case pigeon::core::EngagementIntent::FIRE_AT_TARGET:
        fire(transition, result);
        break;
      case pigeon::core::EngagementIntent::ABANDON:
      case pigeon::core::EngagementIntent::KEEP_SEARCHING:
        break;
    }

    result.state_after = state_.state;
    result.tracks_after = state_.tracks;
    return result;
  }

  [[nodiscard]] const pigeon::core::TargetMachineState& state() const noexcept { return state_; }

 private:
  [[nodiscard]] std::optional<pigeon::core::ServoAngles> angles_for(
      const pigeon::core::Detection& detection) const {
    return pigeon::core::aim_at_centroid(detection.centroid_px, image_size_,
                                         configuration_.camera);
  }

  /// The status the application presents to `authorise_fire` (`REQ-SAF-008`).
  ///
  /// In order: an explicit observation set by a test; otherwise the status of
  /// the most recent exchange if that exchange failed, because a link that has
  /// just failed is not known to be healthy; otherwise `UNAVAILABLE` if the
  /// link says it is not usable; otherwise `OK`.
  [[nodiscard]] pigeon::core::LinkStatus link_health() const {
    if (observed_link_status_.has_value()) {
      return *observed_link_status_;
    }
    if (last_exchange_status_ != pigeon::core::LinkStatus::OK) {
      return last_exchange_status_;
    }
    return link_->is_available() ? pigeon::core::LinkStatus::OK
                                 : pigeon::core::LinkStatus::UNAVAILABLE;
  }

  void aim(const pigeon::core::TargetTransition& transition, FrameOutcome& result) {
    const std::optional<pigeon::core::ServoAngles> angles = angles_for(*transition.aim_at);
    const std::optional<pigeon::core::AimingCommand> command =
        angles.has_value() ? policy_.authorise_aim(*angles)
                           : std::optional<pigeon::core::AimingCommand>{};
    if (!command.has_value()) {
      // An unconfigured rig is commanded nothing at all (`REQ-SAF-004`).
      return;
    }

    result.commanded_aim = command->angles;
    const pigeon::core::LinkStatus status = link_->send_aiming_command(*command);
    last_exchange_status_ = status;
    result.aiming_command_sent = true;

    if (status != pigeon::core::LinkStatus::OK) {
      // The link died between locking and firing: abandon, fire nothing,
      // return to SEARCHING (`REQ-COM-002`).
      state_ = pigeon::core::abandon_engagement(state_);
      result.engagement_abandoned = true;
      last_exchange_status_ = link_->send_safe_state_command();
    }
  }

  void fire(const pigeon::core::TargetTransition& transition, FrameOutcome& result) {
    const std::optional<pigeon::core::ServoAngles> angles = angles_for(*transition.aim_at);
    const pigeon::core::LinkStatus link_status = link_health();
    result.link_status_presented = link_status;
    const pigeon::core::FireAuthorisation authorisation = policy_.authorise_fire(
        transition, angles.value_or(pigeon::core::ServoAngles{}), link_status);

    if (!authorisation.granted().has_value()) {
      // Refused before transmission: no command, and nothing to record
      // (`REQ-SAF-007`).
      result.refusal = authorisation.refusal_reason();
      return;
    }

    result.commanded_aim = authorisation.granted()->aim;
    const pigeon::core::FireCommand command{.duration = authorisation.granted()->duration};
    const pigeon::core::LinkStatus status = link_->send_fire_command(command);
    last_exchange_status_ = status;
    result.fire_status = status;
    result.fire_command_sent = true;
    // Transmitted, so it counts — whatever came back (`REQ-SAF-007`).
    policy_.record_fire_sent();
  }

  pigeon::core::Configuration configuration_{};
  pigeon::core::SafetyPolicy policy_;
  // Non-owning observer: the link outlives the loop.
  pigeon::core::ActuatorLink* link_{nullptr};
  pigeon::core::ImageSize image_size_{};
  pigeon::core::TargetMachineState state_{};
  // Nothing has been exchanged yet, so nothing is known to be wrong.
  pigeon::core::LinkStatus last_exchange_status_{pigeon::core::LinkStatus::OK};
  std::optional<pigeon::core::LinkStatus> observed_link_status_;
};

}  // namespace pigeon::test_support
