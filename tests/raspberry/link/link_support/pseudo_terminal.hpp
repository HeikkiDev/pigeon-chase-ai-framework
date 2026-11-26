#pragma once

// Verifies: REQ-DEV-003, REQ-DEV-001 (support code: a software-only serial line,
// so that the host's serial code is tested on every run without any hardware;
// maintainer ruling Q27, a pseudo-terminal is not physical hardware).

#include <fcntl.h>
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers): posix_openpt and friends are POSIX, not <cstdlib>
#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pigeon::test_support {

/// A pseudo-terminal pair. The code under test opens the **slave** side by path,
/// as it would a serial port; the test plays the device on the **master** side.
///
/// Time never decides a result here: the bounds below only limit how long a
/// *failing* test may hang.
class PseudoTerminal {
 public:
  PseudoTerminal() {
    master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master_ < 0) {
      return;
    }
    if (::grantpt(master_) != 0 || ::unlockpt(master_) != 0) {
      close_master();
      return;
    }
    const char* name = ::ptsname(master_);
    if (name == nullptr) {
      close_master();
      return;
    }
    slave_path_ = name;
    const int flags = ::fcntl(master_, F_GETFL, 0);
    static_cast<void>(::fcntl(master_, F_SETFL, flags | O_NONBLOCK));
  }

  PseudoTerminal(const PseudoTerminal&) = delete;
  PseudoTerminal& operator=(const PseudoTerminal&) = delete;
  PseudoTerminal(PseudoTerminal&&) = delete;
  PseudoTerminal& operator=(PseudoTerminal&&) = delete;

  ~PseudoTerminal() { close_master(); }

  /// Whether the pair could be made. A test asserts this first, so a machine with
  /// no pseudo-terminals fails loudly rather than skipping.
  [[nodiscard]] bool ok() const noexcept { return master_ >= 0; }

  /// What the code under test opens, as it would open a serial port.
  [[nodiscard]] const std::string& slave_path() const noexcept { return slave_path_; }

  /// The device end sends these bytes towards the code under test.
  void send(std::span<const std::uint8_t> bytes) const {
    std::size_t done = 0;
    while (done < bytes.size()) {
      const ssize_t n = ::write(master_, bytes.data() + done, bytes.size() - done);
      if (n < 0) {
        if (errno == EAGAIN || errno == EINTR) {
          static_cast<void>(wait(/*for_read=*/false, 100));
          continue;
        }
        return;
      }
      done += static_cast<std::size_t>(n);
    }
  }

  /// What reaches the device end: up to `count` bytes, waiting at most
  /// `timeout_ms` for the last of them.
  [[nodiscard]] std::vector<std::uint8_t> receive(std::size_t count, int timeout_ms) const {
    std::vector<std::uint8_t> got;
    while (got.size() < count) {
      std::uint8_t chunk[256];
      const ssize_t n = ::read(master_, chunk, std::min(sizeof(chunk), count - got.size()));
      if (n > 0) {
        got.insert(got.end(), chunk, chunk + n);
        continue;
      }
      if (n < 0 && errno != EAGAIN && errno != EINTR) {
        break;
      }
      if (!wait(/*for_read=*/true, timeout_ms)) {
        break;
      }
    }
    return got;
  }

  /// Whatever is waiting at the device end right now, without waiting.
  [[nodiscard]] std::vector<std::uint8_t> receive_now() const { return receive(256, 0); }

  /// The device end goes away, as a pulled cable would.
  void close_master() noexcept {
    if (master_ >= 0) {
      ::close(master_);
      master_ = -1;
    }
  }

 private:
  /// True if the master is ready within `timeout_ms`.
  [[nodiscard]] bool wait(bool for_read, int timeout_ms) const {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(master_, &set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    const int ready = ::select(master_ + 1, for_read ? &set : nullptr, for_read ? nullptr : &set,
                               nullptr, &timeout);
    return ready > 0;
  }

  int master_{-1};
  std::string slave_path_;
};

}  // namespace pigeon::test_support
