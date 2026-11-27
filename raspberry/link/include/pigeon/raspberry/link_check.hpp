#pragma once

#include <optional>
#include <string>
#include <vector>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/aiming.hpp"

namespace pigeon::raspberry {

/// What the hand-run check is asked to do once the link is up
/// (ADR-0021 follow-up 3).
struct LinkCheckOptions {
  /// How many `SAFE_STATE` exchanges to run. The one command that is always safe.
  int safe_state_exchanges{3};
  /// One aiming command to send, if any. It moves the servos, so it is sent only
  /// when asked for; the device checks it against its own envelope and may refuse.
  std::optional<pigeon::core::ServoAngles> aim;
  // There is no option to fire, and no code path that does.
};

/// What the check found, one field per step.
struct LinkCheckReport {
  /// How bringing the link up ended (`start_up`).
  pigeon::core::LinkStatus start_up{pigeon::core::LinkStatus::UNAVAILABLE};
  pigeon::core::LinkStatus health_after_start{pigeon::core::LinkStatus::UNAVAILABLE};
  /// The outcome of each `SAFE_STATE` exchange, in order.
  std::vector<pigeon::core::LinkStatus> safe_state_outcomes;
  /// The outcome of the aiming command, if one was sent.
  std::optional<pigeon::core::LinkStatus> aim_outcome;
  pigeon::core::LinkStatus health_at_end{pigeon::core::LinkStatus::UNAVAILABLE};

  /// The link came up, every `SAFE_STATE` was accepted, an aim (if sent) was
  /// answered by the device, and the link still reads `OK`. A refused aim is a
  /// working safety limit (`REQ-AIM-002`), not a failure, and the link then reads
  /// `REJECTED` (section 7), which that refusal explains; an aim with no answer is
  /// a failure.
  [[nodiscard]] bool passed() const;
};

/// Runs the exchanges of the check on a link that `start_up` has already been
/// called on, with `started` its outcome.
///
/// If the device was not heard (`started` is not `OK`), **nothing is sent**: the
/// specification sends nothing into a silence (`device-protocol.md` section 8),
/// and the report says so. Otherwise it sends the `SAFE_STATE` exchanges and then
/// the aim, if one was asked for. It never sends `FIRE`.
[[nodiscard]] LinkCheckReport run_link_check(pigeon::core::ActuatorLink& link,
                                             pigeon::core::LinkStatus started,
                                             const LinkCheckOptions& options);

/// The report as text for a person: one line per step, then `PASSED` or `FAILED`.
[[nodiscard]] std::string describe(const LinkCheckReport& report);

}  // namespace pigeon::raspberry
