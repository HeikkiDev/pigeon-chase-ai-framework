#include "pigeon/raspberry/link_check_arguments.hpp"

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pigeon::raspberry {

namespace {

/// An integer that is all of `text`, or nothing.
std::optional<long> whole_number(const std::string& text) {
  char* end = nullptr;
  errno = 0;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
    return std::nullopt;
  }
  return value;
}

/// A finite number that is all of `text`, or nothing. `nan` and `inf` are not
/// angles.
std::optional<double> finite_number(const std::string& text) {
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

/// The arguments, taken one at a time.
class Reader {
 public:
  explicit Reader(std::vector<std::string> args) : args_{std::move(args)} {}

  [[nodiscard]] bool has_more() const { return next_ < args_.size(); }

  std::string take() { return args_.at(next_++); }

  /// The next argument, if there is one.
  std::optional<std::string> take_value() {
    if (!has_more()) {
      return std::nullopt;
    }
    return take();
  }

 private:
  std::vector<std::string> args_;
  std::size_t next_{0};
};

/// Each reads the value or values of one option into `arguments` and returns why it
/// cannot, or nothing.
std::string_view read_port(Reader& reader, LinkCheckArguments& arguments) {
  const auto value = reader.take_value();
  if (!value.has_value()) {
    return "--port needs a value";
  }
  arguments.port = *value;
  return {};
}

std::optional<long> take_whole(Reader& reader) {
  const auto value = reader.take_value();
  return value.has_value() ? whole_number(*value) : std::nullopt;
}

/// What a person at a bench could mean. Larger values are a typo, and would hold the
/// board for hours.
constexpr long max_wait_seconds = 3600;
constexpr long max_exchanges = 1000;

std::string_view read_baud(Reader& reader, LinkCheckArguments& arguments) {
  const auto number = take_whole(reader);
  if (!number.has_value() || *number <= 0 ||
      *number > static_cast<long>(std::numeric_limits<unsigned>::max())) {
    return "--baud needs a positive whole number that fits in 32 bits";
  }
  arguments.baud_rate = static_cast<unsigned>(*number);
  return {};
}

std::string_view read_wait(Reader& reader, LinkCheckArguments& arguments) {
  const auto number = take_whole(reader);
  if (!number.has_value() || *number <= 0 || *number > max_wait_seconds) {
    return "--wait-seconds needs a whole number from 1 to 3600";
  }
  arguments.max_wait_seconds = static_cast<int>(*number);
  return {};
}

std::string_view read_exchanges(Reader& reader, LinkCheckArguments& arguments) {
  const auto number = take_whole(reader);
  if (!number.has_value() || *number < 0 || *number > max_exchanges) {
    return "--exchanges needs a whole number from 0 to 1000";
  }
  arguments.options.safe_state_exchanges = static_cast<int>(*number);
  return {};
}

std::string_view read_aim(Reader& reader, LinkCheckArguments& arguments) {
  const auto x = reader.take_value();
  const auto y = reader.take_value();
  const auto x_degrees = x.has_value() ? finite_number(*x) : std::nullopt;
  const auto y_degrees = y.has_value() ? finite_number(*y) : std::nullopt;
  if (!x_degrees.has_value() || !y_degrees.has_value()) {
    return "--aim needs two numbers, X and Y in degrees";
  }
  arguments.options.aim = pigeon::core::ServoAngles{
      .x = pigeon::core::Angle{*x_degrees},
      .y = pigeon::core::Angle{*y_degrees},
  };
  return {};
}

/// The handler for `option`, or none.
using Handler = std::string_view (*)(Reader&, LinkCheckArguments&);

Handler handler_for(const std::string& option) {
  if (option == "--port") {
    return read_port;
  }
  if (option == "--baud") {
    return read_baud;
  }
  if (option == "--wait-seconds") {
    return read_wait;
  }
  if (option == "--exchanges") {
    return read_exchanges;
  }
  if (option == "--aim") {
    return read_aim;
  }
  return nullptr;
}

LinkCheckArguments refused(LinkCheckArguments arguments, std::string_view why) {
  arguments.error = std::string{why};
  return arguments;
}

}  // namespace

std::string_view link_check_usage() {
  return "usage: pigeon_link_check --port PATH [--baud N] [--wait-seconds N]\n"
         "                         [--exchanges N] [--aim X Y]\n"
         "\n"
         "Opens the serial port, waits to hear the device, brings the link up, and runs\n"
         "a few SAFE_STATE exchanges, then says PASSED or FAILED. Run it by hand on a\n"
         "real board; it is never part of the test suite.\n"
         "\n"
         "  --port PATH         the serial port to open (required)\n"
         "  --baud N            line speed, default 115200\n"
         "  --wait-seconds N    how long to wait for the device, default 10\n"
         "  --exchanges N       how many SAFE_STATE exchanges, default 3; 0 checks\n"
         "                      start-up alone\n"
         "  --aim X Y           also send one aiming command, in degrees. It moves the servos,\n"
         "                      so it is sent only when asked, and the device refuses an\n"
         "                      angle outside its own envelope\n"
         "\n"
         "This tool never fires: it has no option and no code path that sends FIRE.\n"
         "If the device is not heard it sends no commands; closing the link still sends\n"
         "one SAFE_STATE, which is always safe.\n";
}

LinkCheckArguments parse_link_check_arguments(const std::vector<std::string>& args) {
  LinkCheckArguments parsed;
  Reader reader{args};

  while (reader.has_more()) {
    const std::string option = reader.take();
    if (option == "--help") {
      parsed.help = true;
      return parsed;
    }
    const Handler handler = handler_for(option);
    if (handler == nullptr) {
      return refused(std::move(parsed), std::string{"unknown option "} + option);
    }
    const std::string_view why = handler(reader, parsed);
    if (!why.empty()) {
      return refused(std::move(parsed), why);
    }
  }

  if (parsed.port.empty()) {
    return refused(std::move(parsed), "--port is required");
  }
  return parsed;
}

}  // namespace pigeon::raspberry
