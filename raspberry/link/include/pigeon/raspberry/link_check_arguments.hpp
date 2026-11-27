#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pigeon/raspberry/link_check.hpp"

namespace pigeon::raspberry {

/// The command line of the hand-run check, parsed (ADR-0021 follow-up 3).
struct LinkCheckArguments {
  /// The serial port to open, e.g. the Arduino's device node.
  std::string port;
  unsigned baud_rate{115200};
  /// How long to wait to hear the device and bring the link up.
  int max_wait_seconds{10};
  LinkCheckOptions options;
  /// `--help` was given: print the usage and run nothing. Not an error.
  bool help{false};
  /// Why the arguments were refused, or empty.
  std::string error;

  /// The arguments are good and there is something to run.
  [[nodiscard]] bool ok() const { return error.empty() && !help; }
};

/// Parses the arguments after the program name. A mistyped argument is refused with
/// a message that names it, never guessed at: the tool runs on real hardware.
[[nodiscard]] LinkCheckArguments parse_link_check_arguments(const std::vector<std::string>& args);

/// What `--help` prints, and what a refused command line is followed by.
[[nodiscard]] std::string_view link_check_usage();

}  // namespace pigeon::raspberry
