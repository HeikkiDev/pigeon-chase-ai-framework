#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/aiming.hpp"
#include "pigeon/core/clock.hpp"
#include "pigeon/core/configuration.hpp"
#include "pigeon/core/target_state.hpp"

namespace pigeon::core {

/// Why a fire command was refused.
///
/// Every refusal has a named reason, because "it did not fire" is not a
/// diagnosis, and a safety mechanism that cannot say which rule it applied
/// cannot be shown to have applied the right one.
enum class FireRefusal : std::uint8_t {
  /// The engagement has not satisfied confirmation and re-verification
  /// (`REQ-SAF-002`, `REQ-TRK-002`, `REQ-TRK-004`). Also the default.
  NOT_CONFIRMED,
  /// The mechanical envelope is empty, so nothing may be commanded at all —
  /// the state of an unconfigured system (`REQ-AIM-002`, `REQ-SAF-004`).
  NO_ENVELOPE,
  /// The clamped aim lies inside a configured exclusion zone (`REQ-SAF-003`).
  EXCLUSION_ZONE,
  /// The cool-down since the last fire has not elapsed (`REQ-SAF-005`).
  COOLING_DOWN,
  /// The maximum number of engagements in the rate window has been reached
  /// (`REQ-SAF-005`).
  RATE_LIMIT_REACHED,
  /// The actuator link is unavailable, so the engagement is abandoned
  /// (`REQ-COM-002`).
  LINK_UNAVAILABLE,
};

/// The terms of an authorised burst: where, and for how long.
struct GrantedFire {
  /// Aim point in the servo frame, already clamped to the mechanical envelope
  /// and already checked against the exclusion zone (`REQ-AIM-002`,
  /// `REQ-SAF-003`).
  ServoAngles aim{};

  /// Burst length, never longer than the configured maximum (`REQ-SAF-001`).
  std::chrono::milliseconds duration{0};
};

/// The answer to "may this engagement fire?".
///
/// Refusal is the default and the only state a default-constructed value can
/// be in, so a forgotten assignment or an unhandled branch denies the burst
/// rather than granting one (`REQ-SAF-004`). Permission is carried by the
/// presence of the terms, not by a separate flag that could contradict them.
class FireAuthorisation {
 public:
  /// Refused, `NOT_CONFIRMED`: the safe default.
  FireAuthorisation() = default;

  /// Authorise a burst on the stated terms.
  [[nodiscard]] static FireAuthorisation grant(GrantedFire fire);

  /// Refuse, recording which rule refused.
  [[nodiscard]] static FireAuthorisation refuse(FireRefusal reason);

  /// The authorised terms, or `std::nullopt` if the burst was refused.
  [[nodiscard]] const std::optional<GrantedFire>& granted() const noexcept;

  /// Which rule refused. Meaningful only when `granted()` is empty.
  [[nodiscard]] FireRefusal refusal_reason() const noexcept;

 private:
  std::optional<GrantedFire> granted_;
  FireRefusal reason_{FireRefusal::NOT_CONFIRMED};
};

/// The guard between an intent to fire and a fire command.
///
/// The target state machine decides *whether the bird is there*
/// (`target_state.hpp`); this class decides *whether firing at it is
/// permitted*. Splitting the two keeps the state machine a pure function and
/// puts every safety rule that needs elapsed time, configuration or link state
/// in one auditable place:
///
/// | Rule                                  | Requirement   |
/// | ------------------------------------- | ------------- |
/// | Confirmed and re-verified engagement  | `REQ-SAF-002` |
/// | Angles clamped to the envelope        | `REQ-AIM-002` |
/// | Exclusion zone                        | `REQ-SAF-003` |
/// | Cool-down and engagement rate         | `REQ-SAF-005`, `REQ-SAF-007` |
/// | Bounded burst duration                | `REQ-SAF-001` |
/// | Link failure abandons the engagement  | `REQ-COM-002` |
///
/// Time is read only through the injected `MonotonicClock`, never from the
/// wall clock, so rate limiting is tested by advancing a simulated clock
/// rather than by waiting (ADR-0005, `REQ-DEV-002`).
///
/// This is the *commanding* side of the safety limits. The Arduino enforces
/// the envelope and the burst bound again on receipt, because a limit that
/// only the commanding device enforces fails exactly when that device does.
class SafetyPolicy {
 public:
  /// Construct with the installation's configuration and a clock.
  ///
  /// The clock is observed, not owned, and must outlive this policy. A
  /// default-constructed `Configuration` yields a policy that refuses every
  /// burst, which is the required behaviour of an unconfigured system
  /// (`REQ-SAF-004`).
  SafetyPolicy(Configuration configuration, const MonotonicClock& clock);

  /// Turn computed angles into an aiming command, or into nothing
  /// (`REQ-AIM-002`).
  ///
  /// Clamps to the mechanical envelope. Returns `std::nullopt` when the
  /// envelope is empty, so an unconfigured rig is never commanded to move.
  /// Aiming is not gated by the exclusion zone or the rate limit: pointing is
  /// not firing.
  [[nodiscard]] std::optional<AimingCommand> authorise_aim(ServoAngles requested) const;

  /// Decide whether a fire intent may become a fire command.
  ///
  /// Refuses unless `transition` carries `EngagementIntent::FIRE_AT_TARGET`,
  /// which the state machine produces only after confirmation and
  /// re-verification (`REQ-SAF-002`). Then, in order: link availability
  /// (`REQ-COM-002`), envelope clamp (`REQ-AIM-002`), the exclusion zone
  /// (`REQ-SAF-003`), cool-down and engagement rate (`REQ-SAF-005`). A granted
  /// authorisation carries the clamped aim and a duration bounded by
  /// `SafetyLimits::max_fire_duration` (`REQ-SAF-001`).
  ///
  /// Reads the injected clock. Does **not** record anything: authorising is
  /// not firing, and a burst that was never transmitted must not start a
  /// cool-down.
  ///
  /// A refusal does not un-end the engagement. The state machine has already
  /// returned to `SEARCHING` and retired the engaged track (`REQ-TRK-011`,
  /// `REQ-TRK-012`), and it cannot hear this answer, so a bird whose burst was
  /// refused must earn three fresh consecutive detections before it can be
  /// confirmed again. That is the conservative direction, and it is why a
  /// cooling-down rig does not re-aim at the same bird on every frame.
  [[nodiscard]] FireAuthorisation authorise_fire(const TargetTransition& transition,
                                                 ServoAngles requested_aim,
                                                 LinkStatus link_status) const;

  /// Record that a fire command was **transmitted** to the actuator link,
  /// starting the cool-down and counting against the engagement rate
  /// (`REQ-SAF-005`, `REQ-SAF-007`).
  ///
  /// Called once per command handed to `ActuatorLink::send_fire_command`,
  /// whatever `LinkStatus` comes back, because a rejected or dropped reply
  /// does not tell you whether water left the nozzle (ADR-0011). The only
  /// command that must **not** be recorded is one that was never sent —
  /// a refusal from `authorise_fire`.
  ///
  /// Separate from `authorise_fire` so that authorising stays a `const` query
  /// and the rate limiter counts transmissions rather than permissions. Reads
  /// the injected clock for the timestamp.
  void record_fire_sent();

 private:
  Configuration configuration_{};
  // Non-owning observer: the clock outlives the policy (cpp.instructions.md).
  const MonotonicClock* clock_{nullptr};
  // Timestamps of transmitted bursts, for the cool-down and the rate window.
  std::vector<MonotonicTimePoint> sent_fires_;
};

}  // namespace pigeon::core
