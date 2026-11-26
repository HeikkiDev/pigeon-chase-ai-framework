#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "pigeon/raspberry/byte_stream.hpp"

namespace pigeon::raspberry {

/// How the port is set up. Only the line speed is chosen: the protocol's
/// reference setting is 8 data bits, no parity, 1 stop bit, no flow control
/// (`docs/architecture/device-protocol.md`, section 2), and those are not options.
struct SerialSettings {
  /// Bits per second. One of 9600, 19200, 38400, 57600 or 115200, the speeds a
  /// Raspberry Pi and an Arduino share; the reference setting is 115200. Anything
  /// else is refused rather than rounded.
  unsigned baud_rate{115200};
  /// How long `write` waits for a port that will not take bytes before giving up.
  /// Under the 100 ms reply timeout: a stuck port must fault an exchange, not
  /// freeze the caller.
  std::chrono::milliseconds write_timeout{100};
};

/// A serial port, as a `ByteStream` (`REQ-COM-001`, ADR-0021 follow-up 3).
///
/// POSIX only (`open`, `termios`, `select`), so it builds on the Raspberry Pi and
/// on macOS and needs no third-party library. The port is put in **raw mode**: no
/// byte value is translated, echoed, taken for a signal or swallowed as flow
/// control, because the protocol is binary and `0x00` is its delimiter.
///
/// It is opened by the constructor. If that fails, `is_open()` is false and
/// `open_error()` holds the `errno` (`ENOENT` for no such path, `ENOTTY` for
/// something that is not a terminal, `EINVAL` for a line speed it does not know).
/// A port that is lost later (the cable pulled, the adapter gone) stops being
/// open, so that `ActuatorLink::health()` reads `UNAVAILABLE` (`REQ-COM-002`).
///
/// Not thread-safe, like the link above it. The destructor closes the port.
class SerialPortStream final : public ByteStream {
 public:
  explicit SerialPortStream(const std::string& path, SerialSettings settings = {});

  SerialPortStream(const SerialPortStream&) = delete;
  SerialPortStream& operator=(const SerialPortStream&) = delete;
  SerialPortStream(SerialPortStream&&) = delete;
  SerialPortStream& operator=(SerialPortStream&&) = delete;

  ~SerialPortStream() override;

  [[nodiscard]] bool is_open() const noexcept override;

  /// Never waits. Zero means nothing has arrived, or that the port is lost, in
  /// which case it is no longer open.
  [[nodiscard]] std::size_t read(std::span<std::uint8_t> out) noexcept override;

  /// Sends every byte, waiting up to the write timeout for a port that is full.
  /// False if it could not, and the port is closed if it has been lost.
  [[nodiscard]] bool write(std::span<const std::uint8_t> bytes) noexcept override;

  /// Returns when a byte can be read, the port is lost, or `max` has passed. A
  /// port that is not open is not waited on.
  void wait_readable(std::chrono::milliseconds max) noexcept override;

  /// Closes the port. Harmless if it is already closed.
  void close() noexcept;

  /// Why the port could not be opened (`errno`), or zero if it could.
  [[nodiscard]] int open_error() const noexcept { return open_error_; }

 private:
  int fd_{-1};
  int open_error_{0};
  SerialSettings settings_;
};

}  // namespace pigeon::raspberry
