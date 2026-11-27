// Hand-run check of the host link against a real Arduino (ADR-0021 follow-up 3).
//
// Opens a serial port, waits to hear the device, brings the link up and runs a few
// SAFE_STATE exchanges, then says PASSED or FAILED. It is never registered with
// CTest, so it cannot run in CI by construction (REQ-DEV-003): it needs a board.
//
// It never fires. It has no option and no code path that sends FIRE; moving the
// servos needs an explicit --aim. If the device is not heard it sends nothing.
//
// Exit status: 0 PASSED, 1 FAILED, 2 bad arguments, 3 the port could not be opened,
// 4 an unexpected error (the link is still shut down: its destructors run).

#include <cstring>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "pigeon/raspberry/device_link.hpp"
#include "pigeon/raspberry/link_check.hpp"
#include "pigeon/raspberry/link_check_arguments.hpp"

namespace {

int run(const std::vector<std::string>& args) {
  using namespace pigeon::raspberry;  // NOLINT(google-build-using-namespace): a tool's main

  const LinkCheckArguments parsed = parse_link_check_arguments(args);
  if (parsed.help) {
    std::cout << link_check_usage();
    return 0;
  }
  if (!parsed.ok()) {
    std::cerr << "pigeon_link_check: " << parsed.error << "\n\n" << link_check_usage();
    return 2;
  }

  DeviceLinkSettings settings;
  settings.serial.baud_rate = parsed.baud_rate;
  std::cout << "opening " << parsed.port << " at " << parsed.baud_rate << " baud\n";
  DeviceLink device{parsed.port, settings};
  if (!device.is_open()) {
    std::cerr << "pigeon_link_check: cannot open " << parsed.port << ": "
              << std::strerror(device.open_error()) << '\n';
    return 3;
  }

  std::cout << "waiting up to " << parsed.max_wait_seconds << " s to hear the device\n";
  const auto started = device.start(std::chrono::seconds{parsed.max_wait_seconds});
  const LinkCheckReport report = run_link_check(device.link(), started, parsed.options);
  std::cout << describe(report);
  return report.passed() ? 0 : 1;
  // Leaving this scope stops the reader, sends SAFE_STATE and closes the port.
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::span<char*> all{argv, static_cast<std::size_t>(argc)};
    std::vector<std::string> args;
    for (const char* argument : all.subspan(1)) {
      args.emplace_back(argument);
    }
    return run(args);
  } catch (const std::exception& error) {
    std::cerr << "pigeon_link_check: unexpected error: " << error.what() << '\n';
  } catch (...) {
    std::cerr << "pigeon_link_check: unexpected error\n";
  }
  return 4;
}
