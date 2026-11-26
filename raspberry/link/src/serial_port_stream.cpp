#include "pigeon/raspberry/serial_port_stream.hpp"

#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace pigeon::raspberry {

namespace {

std::optional<speed_t> speed_for(unsigned baud_rate) noexcept {
  switch (baud_rate) {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
    default:
      return std::nullopt;
  }
}

/// Waits until `fd` can be read or written, or `timeout` has passed. True if it
/// became ready. An interrupted wait is not a result: it reports false and the
/// caller looks again.
bool wait_for(int fd, bool for_read, std::chrono::milliseconds timeout) noexcept {
  const auto clamped = std::max(timeout, std::chrono::milliseconds{0});
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(clamped);
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(clamped - seconds);
  timeval limit{};
  limit.tv_sec = static_cast<decltype(limit.tv_sec)>(seconds.count());
  limit.tv_usec = static_cast<decltype(limit.tv_usec)>(micros.count());

  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,hicpp-signed-bitwise)
  // FD_SET and FD_ZERO are macros over a bit array; fd < FD_SETSIZE is checked on open.
  fd_set set;
  FD_ZERO(&set);
  FD_SET(fd, &set);
  // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,hicpp-signed-bitwise)
  const int ready =
      ::select(fd + 1, for_read ? &set : nullptr, for_read ? nullptr : &set, nullptr, &limit);
  return ready > 0;
}

}  // namespace

SerialPortStream::SerialPortStream(const std::string& path, SerialSettings settings)
    : settings_{settings} {
  const std::optional<speed_t> speed = speed_for(settings.baud_rate);
  if (!speed.has_value()) {
    open_error_ = EINVAL;
    return;
  }

  // O_NOCTTY: the port must never become this process's controlling terminal.
  // O_NONBLOCK: `read` never waits.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) is variadic
  const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    open_error_ = errno;
    return;
  }
  if (fd >= FD_SETSIZE) {  // select(2) cannot watch it
    ::close(fd);
    open_error_ = EMFILE;
    return;
  }

  termios tty{};
  if (::tcgetattr(fd, &tty) != 0) {
    open_error_ = errno;  // ENOTTY: it is not a terminal at all
    ::close(fd);
    return;
  }

  // Raw: no translation, echo, signals or software flow control. Then the
  // protocol's setting: 8 data bits, no parity, 1 stop bit, no hardware flow
  // control, and the receiver on and modem lines ignored.
  ::cfmakeraw(&tty);
  tty.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
  // Converted to the unsigned flag type before it is negated: on macOS these are
  // plain ints, and a bitwise NOT of a signed value is what clang-tidy rejects.
  const auto unwanted = static_cast<tcflag_t>(CSTOPB | CRTSCTS);
  tty.c_cflag &= ~unwanted;
  // A read returns what has arrived and, with the descriptor non-blocking, reports
  // "nothing yet" as EAGAIN. That leaves a read of zero bytes to mean one thing
  // only: the other end has gone.
  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 0;

  if (::cfsetspeed(&tty, *speed) != 0 || ::tcsetattr(fd, TCSANOW, &tty) != 0) {
    open_error_ = errno;
    ::close(fd);
    return;
  }
  // Anything that arrived before the port was set up is not part of this session.
  static_cast<void>(::tcflush(fd, TCIOFLUSH));

  fd_ = fd;
}

SerialPortStream::~SerialPortStream() { close(); }

bool SerialPortStream::is_open() const noexcept { return fd_ >= 0; }

std::size_t SerialPortStream::read(std::span<std::uint8_t> out) noexcept {
  if (fd_ < 0 || out.empty()) {
    return 0;
  }
  const ssize_t n = ::read(fd_, out.data(), out.size());
  if (n > 0) {
    return static_cast<std::size_t>(n);
  }
  // POSIX lets EWOULDBLOCK differ from EAGAIN; on Linux and macOS they are one value.
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
    return 0;  // nothing has arrived: not a fault
  }
  close();  // end of file, or any other error: the line is lost
  return 0;
}

bool SerialPortStream::write(std::span<const std::uint8_t> bytes) noexcept {
  if (fd_ < 0) {
    return false;
  }
  const auto deadline = std::chrono::steady_clock::now() + settings_.write_timeout;
  std::size_t done = 0;
  while (done < bytes.size()) {
    const std::span<const std::uint8_t> rest = bytes.subspan(done);
    const ssize_t n = ::write(fd_, rest.data(), rest.size());
    if (n > 0) {
      done += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && errno == EAGAIN) {  // the port is full: wait for room, but not for ever
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now());
      if (remaining <= std::chrono::milliseconds{0}) {
        return false;
      }
      static_cast<void>(wait_for(fd_, /*for_read=*/false, remaining));
      continue;
    }
    close();  // the line is lost
    return false;
  }
  return true;
}

void SerialPortStream::wait_readable(std::chrono::milliseconds max) noexcept {
  if (fd_ < 0) {
    return;
  }
  static_cast<void>(wait_for(fd_, /*for_read=*/true, max));
}

void SerialPortStream::close() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

}  // namespace pigeon::raspberry
