#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace pigeon::raspberry {

/// The line between the Raspberry Pi and the Arduino, as the host link sees it
/// (`docs/architecture/device-protocol.md`, section 2).
///
/// A stream of bytes that may corrupt, drop or duplicate them but not reorder
/// them. In deployment it is a serial port; in the default test suite it is a
/// simulated device, because the suite opens no device node (`REQ-DEV-001`).
///
/// Nothing here blocks except `wait_readable`, and nothing throws: a failure is
/// a return value, because the caller has to *act* on it (`REQ-COM-002`).
class ByteStream {
 public:
  ByteStream() = default;
  ByteStream(const ByteStream&) = delete;
  ByteStream& operator=(const ByteStream&) = delete;
  ByteStream(ByteStream&&) = delete;
  ByteStream& operator=(ByteStream&&) = delete;
  virtual ~ByteStream() = default;

  /// Whether the line is open. A closed line carries nothing in either
  /// direction.
  [[nodiscard]] virtual bool is_open() const noexcept = 0;

  /// Copies the bytes that have already arrived into `out` and returns how many.
  /// Never waits: zero means nothing is there now.
  [[nodiscard]] virtual std::size_t read(std::span<std::uint8_t> out) noexcept = 0;

  /// Sends all of `bytes`. False on any error or a short write, in which case
  /// the caller must assume the device saw nothing, or part of it.
  [[nodiscard]] virtual bool write(std::span<const std::uint8_t> bytes) noexcept = 0;

  /// Returns when a byte can be read or after `max`, whichever comes first.
  ///
  /// The only place time passes. A real stream sleeps in the operating system; a
  /// simulated one advances its injected clock and lets the simulated device act,
  /// which is what keeps every timing in the link testable without waiting
  /// (`REQ-DEV-002`).
  virtual void wait_readable(std::chrono::milliseconds max) noexcept = 0;
};

}  // namespace pigeon::raspberry
