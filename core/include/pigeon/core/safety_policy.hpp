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
/// cannot be shown to have applied the right one. The link-related reasons are
/// one-to-one with the values of `LinkStatus`, so a status can neither be
/// unhandled nor quietly inherit another status's reason (`REQ-SAF-008`).
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
  /// The actuator link is not open, or has been lost: `LinkStatus::UNAVAILABLE`
  /// (`REQ-COM-002`, `REQ-SAF-008`).
  LINK_UNAVAILABLE,
  /// The link is open but the exchange failed: `LinkStatus::TRANSPORT_FAILURE`
  /// (`REQ-SAF-008`).
  LINK_TRANSPORT_FAILURE,
  /// The device understood an earlier command and refused it:
  /// `LinkStatus::REJECTED` (`REQ-SAF-008`).
  LINK_REJECTED,
};

/// The refusal a link status produces, or `std::nullopt` for the one status
/// that does not refuse (`REQ-SAF-008`, ADR-0015).
///
/// One mapping, in one place, so that "which statuses may fire?" has exactly
/// one answer in the codebase. Only `LinkStatus::OK` yields `std::nullopt`;
/// every other status yields its own reason, shared with no other status, so a
/// refusal in a log names the thing that went wrong.
///
/// **Its definition is a `switch` over `LinkStatus` with no `default:`
/// label.** That is what keeps this gap closed: `-Wswitch` under the project's
/// `-Werror` refuses to compile a `LinkStatus` enumerator that has been given
/// no answer here. A `default:` label in that switch would silently reopen it,
/// so a reviewer has one line to look for. The assertion below is the second
/// half of the mechanism: it catches an enumerator inserted, reordered or
/// removed rather than appended, and it fails in this header, next to the
/// reasons themselves.
[[nodiscard]] std::optional<FireRefusal> refusal_for(LinkStatus status) noexcept;

static_assert(static_cast<std::uint8_t>(LinkStatus::OK) == 0U &&
                  static_cast<std::uint8_t>(LinkStatus::UNAVAILABLE) == 1U &&
                  static_cast<std::uint8_t>(LinkStatus::TRANSPORT_FAILURE) == 2U &&
                  static_cast<std::uint8_t>(LinkStatus::REJECTED) == 3U,
              "LinkStatus has changed shape. Every status that is not OK needs its own "
              "FireRefusal and its own case in refusal_for(); give the new one both, then "
              "update this assertion (REQ-SAF-008, ADR-0015).");

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
/// | Only a healthy link may fire          | `REQ-SAF-008` |
/// | Link failure abandons the engagement  | `REQ-COM-002` |
///
/// Time is read only through the injected `MonotonicClock`, never from the
/// wall clock, so rate limiting is tested by advancing a simulated clock
/// rather than by waiting (ADR-0005, `REQ-DEV-002`).
///
/// **One convention for elapsed time**, used by the cool-down and the rate
/// window alike (`REQ-SAF-005`, ADR-0014):
///
/// * a period of duration *D* that began at *t* has **elapsed** once the clock
///   reads *t + D* or later — the comparison is `now - t >= D`, so *t + D* is
///   the first permitted instant;
/// * a window of length *D* ending at *now* is **half-open**: it contains
///   every recorded instant strictly later than *now - D*, so a burst exactly
///   *D* old has left the window and no longer counts.
///
/// Two limits, one rule, because two rules drift apart.
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
  /// re-verification (`REQ-SAF-002`). Then, in order: link health
  /// (`REQ-SAF-008`, `REQ-COM-002`), envelope clamp (`REQ-AIM-002`), the
  /// exclusion zone (`REQ-SAF-003`), cool-down and engagement rate
  /// (`REQ-SAF-005`). A granted authorisation carries the clamped aim and a
  /// duration bounded by `SafetyLimits::max_fire_duration` (`REQ-SAF-001`).
  ///
  /// `link_status` grants only when it is `LinkStatus::OK`. Every other status
  /// refuses with `*refusal_for(link_status)` — its own reason, never shared
  /// with another status (`REQ-SAF-008`, ADR-0015). A link that is not known to
  /// be healthy is not a link to send water over.
  ///
  /// The cool-down and rate checks use the elapsed-time convention documented
  /// on this class: a burst is permitted at exactly the cool-down duration
  /// after the previous transmitted one, and a burst exactly one minute old has
  /// already left the rate window (`REQ-SAF-005`, ADR-0014).
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
  /// the injected clock for the timestamp, which is the instant the cool-down
  /// and the rate window are measured from (`REQ-SAF-005`, ADR-0014).
  void record_fire_sent();

 private:
  Configuration configuration_{};
  // Non-owning observer: the clock outlives the policy (cpp.instructions.md).
  const MonotonicClock* clock_{nullptr};
  // Timestamps of transmitted bursts, for the cool-down and the half-open rate
  // window. Entries older than the window may be dropped: they can never again
  // affect an answer.
  std::vector<MonotonicTimePoint> sent_fires_;
};

}  // namespace pigeon::core
