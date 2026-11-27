#include "pigeon/raspberry/link_check.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>

namespace pigeon::raspberry {

using pigeon::core::LinkStatus;

namespace {

const char* name_of(LinkStatus status) noexcept {
  switch (status) {
    case LinkStatus::OK:
      return "OK";
    case LinkStatus::UNAVAILABLE:
      return "UNAVAILABLE";
    case LinkStatus::TRANSPORT_FAILURE:
      return "TRANSPORT_FAILURE";
    case LinkStatus::REJECTED:
      return "REJECTED";
  }
  return "UNKNOWN";
}

}  // namespace

bool LinkCheckReport::passed() const {
  const bool all_safe_states_accepted = std::ranges::all_of(
      safe_state_outcomes, [](LinkStatus outcome) { return outcome == LinkStatus::OK; });
  const bool aim_answered = !aim_outcome.has_value() || *aim_outcome == LinkStatus::OK ||
                            *aim_outcome == LinkStatus::REJECTED;
  // A refused aim leaves the link reading REJECTED (section 7): that is the device's
  // limit working, and is explained by the refusal, so it is not held against it.
  const bool health_explained =
      health_at_end == LinkStatus::OK ||
      (health_at_end == LinkStatus::REJECTED && aim_outcome == LinkStatus::REJECTED);
  return start_up == LinkStatus::OK && all_safe_states_accepted && aim_answered && health_explained;
}

LinkCheckReport run_link_check(pigeon::core::ActuatorLink& link, LinkStatus started,
                               const LinkCheckOptions& options) {
  LinkCheckReport report;
  report.start_up = started;
  report.health_after_start = link.health();

  if (started == LinkStatus::OK) {
    for (int i = 0; i < options.safe_state_exchanges; ++i) {
      report.safe_state_outcomes.push_back(link.send_safe_state_command());
    }
    if (options.aim.has_value()) {
      pigeon::core::AimingCommand command;
      command.angles = *options.aim;
      report.aim_outcome = link.send_aiming_command(command);
    }
  }
  report.health_at_end = link.health();
  return report;
}

std::string describe(const LinkCheckReport& report) {
  std::ostringstream text;
  text << "start-up: " << name_of(report.start_up) << '\n';
  if (report.start_up != LinkStatus::OK) {
    text << "  the device was not heard, so nothing was sent\n";
  }
  text << "health after start-up: " << name_of(report.health_after_start) << '\n';
  std::size_t number = 0;
  for (const LinkStatus outcome : report.safe_state_outcomes) {
    text << "SAFE_STATE " << ++number << ": " << name_of(outcome) << '\n';
  }
  if (report.aim_outcome.has_value()) {
    text << "AIM: " << name_of(*report.aim_outcome) << '\n';
  }
  text << "health at end: " << name_of(report.health_at_end);
  if (report.health_at_end == LinkStatus::REJECTED && report.aim_outcome == LinkStatus::REJECTED) {
    text << " (the device refused the aim, as its limits require)";
  }
  text << '\n';
  text << (report.passed() ? "PASSED" : "FAILED") << '\n';
  return text.str();
}

}  // namespace pigeon::raspberry
