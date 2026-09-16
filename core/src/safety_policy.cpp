#include "pigeon/core/safety_policy.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/aiming.hpp"
#include "pigeon/core/clock.hpp"
#include "pigeon/core/configuration.hpp"
#include "pigeon/core/target_state.hpp"

namespace pigeon::core {
namespace {

/// The length of the engagement-rate window: "6 engagements per minute"
/// (`REQ-SAF-005`). A property of the requirement rather than of the rig, so it
/// is a constant here and not a configured parameter.
constexpr std::chrono::milliseconds rate_window{60000};

/// Has a period of `duration` that began at `started` elapsed by `now`?
///
/// The one elapsed-time convention: `now - started >= duration`, so the period
/// beginning at *t* occupies [*t*, *t* + *D*) and *t* + *D* is the first
/// permitted instant (`REQ-SAF-005`, ADR-0014).
[[nodiscard]] bool has_elapsed(MonotonicTimePoint started, MonotonicTimePoint now,
                               std::chrono::milliseconds duration) noexcept {
  return now.since_epoch - started.since_epoch >= duration;
}

/// Is `instant` inside the half-open window of length `window` ending at `now`?
///
/// The window contains every instant strictly later than *now* − *D*, so a
/// burst exactly *D* old has left it (`REQ-SAF-005`, ADR-0014). Expressed as
/// the negation of `has_elapsed` so that the two limits cannot drift apart.
[[nodiscard]] bool is_within_window(MonotonicTimePoint instant, MonotonicTimePoint now,
                                    std::chrono::milliseconds window) noexcept {
  return !has_elapsed(instant, now, window);
}

}  // namespace

std::optional<FireRefusal> refusal_for(LinkStatus status) noexcept {
  // Exhaustive, and deliberately without a `default:` label: -Wswitch under
  // -Werror is what guarantees that a status added later cannot be left without
  // a refusal of its own (`REQ-SAF-008`, ADR-0015).
  switch (status) {
    case LinkStatus::OK:
      return std::nullopt;
    case LinkStatus::TRANSPORT_FAILURE:
      return FireRefusal::LINK_TRANSPORT_FAILURE;
    case LinkStatus::UNAVAILABLE:
      return FireRefusal::LINK_UNAVAILABLE;
    case LinkStatus::REJECTED:
      return FireRefusal::LINK_REJECTED;
  }
  // Only reachable through a value that is not a `LinkStatus` at all. A link in
  // a condition this code cannot name is not a link to send water over.
  return FireRefusal::LINK_UNAVAILABLE;
}

FireAuthorisation FireAuthorisation::grant(GrantedFire fire) {
  FireAuthorisation authorisation;
  authorisation.granted_ = fire;
  return authorisation;
}

FireAuthorisation FireAuthorisation::refuse(FireRefusal reason) {
  FireAuthorisation authorisation;
  authorisation.reason_ = reason;
  return authorisation;
}

const std::optional<GrantedFire>& FireAuthorisation::granted() const noexcept { return granted_; }

FireRefusal FireAuthorisation::refusal_reason() const noexcept { return reason_; }

SafetyPolicy::SafetyPolicy(Configuration configuration, const MonotonicClock& clock)
    : configuration_{configuration}, clock_{&clock} {}

std::optional<AimingCommand> SafetyPolicy::authorise_aim(ServoAngles requested) const {
  const std::optional<ServoAngles> clamped = clamp_to_envelope(requested, configuration_.envelope);
  if (!clamped.has_value()) {
    return std::nullopt;
  }
  return AimingCommand{.angles = *clamped};
}

FireAuthorisation SafetyPolicy::authorise_fire(const TargetTransition& transition,
                                               ServoAngles requested_aim,
                                               const ActuatorLink& link) const {
  // Only the state machine's fire intent, which it produces only after
  // confirmation and re-verification (`REQ-SAF-002`).
  if (transition.intent != EngagementIntent::FIRE_AT_TARGET) {
    return FireAuthorisation::refuse(FireRefusal::NOT_CONFIRMED);
  }

  // Asked of the link, here, at the instant of the decision — never remembered
  // from an earlier exchange (`REQ-COM-003`, `REQ-SAF-008`, ADR-0016).
  if (const std::optional<FireRefusal> unhealthy = refusal_for(link.health());
      unhealthy.has_value()) {
    return FireAuthorisation::refuse(*unhealthy);
  }

  const std::optional<ServoAngles> aim = clamp_to_envelope(requested_aim, configuration_.envelope);
  if (!aim.has_value()) {
    return FireAuthorisation::refuse(FireRefusal::NO_ENVELOPE);
  }

  // A zone is the conjunction of its two axis ranges (`REQ-SAF-003`,
  // `REQ-SAF-006`).
  const std::optional<ExclusionZone>& zone = configuration_.safety.exclusion_zone;
  if (zone.has_value() && zone->x.contains(aim->x) && zone->y.contains(aim->y)) {
    return FireAuthorisation::refuse(FireRefusal::EXCLUSION_ZONE);
  }

  const MonotonicTimePoint now = clock_->now();
  if (!sent_fires_.empty() &&
      !has_elapsed(sent_fires_.back(), now, configuration_.safety.cool_down)) {
    return FireAuthorisation::refuse(FireRefusal::COOLING_DOWN);
  }

  const auto in_window =
      static_cast<std::uint32_t>(std::ranges::count_if(sent_fires_, [now](MonotonicTimePoint sent) {
        return is_within_window(sent, now, rate_window);
      }));
  if (in_window >= configuration_.safety.max_engagements_per_minute) {
    return FireAuthorisation::refuse(FireRefusal::RATE_LIMIT_REACHED);
  }

  // Authorising is not firing: nothing is recorded here (`REQ-SAF-007`).
  return FireAuthorisation::grant(
      GrantedFire{.aim = *aim, .duration = configuration_.safety.max_fire_duration});
}

void SafetyPolicy::record_fire_sent() {
  const MonotonicTimePoint now = clock_->now();

  // A burst that can no longer affect either limit is forgotten. The horizon is
  // the longer of the two so that a cool-down configured beyond the rate window
  // still sees the burst that started it (`REQ-SAF-005`).
  const std::chrono::milliseconds horizon = std::max(rate_window, configuration_.safety.cool_down);
  const auto expired = std::ranges::remove_if(sent_fires_, [now, horizon](MonotonicTimePoint sent) {
    return has_elapsed(sent, now, horizon);
  });
  sent_fires_.erase(expired.begin(), expired.end());

  // Transmitted, so it counts, whatever the device said about it
  // (`REQ-SAF-007`, ADR-0011).
  sent_fires_.push_back(now);
}

}  // namespace pigeon::core
