#pragma once

// Verifies: REQ-DEV-001 (support code — a byte stream with no port behind it, so
// that no test opens a device node), used by REQ-COM-001, REQ-COM-003.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pigeon/raspberry/byte_stream.hpp"

namespace pigeon::test_support {

/// A line that carries nothing in and remembers everything sent out.
///
/// The simplest possible `ByteStream`: enough to prove what a link does *not*
/// do (transmit, for example) before there is anything on the line to answer.
class RecordingStream final : public pigeon::raspberry::ByteStream {
 public:
  explicit RecordingStream(bool open = true) : open_{open} {}

  [[nodiscard]] bool is_open() const noexcept override { return open_; }

  [[nodiscard]] std::size_t read(std::span<std::uint8_t> /*out*/) noexcept override { return 0; }

  [[nodiscard]] bool write(std::span<const std::uint8_t> bytes) noexcept override {
    bytes_written_ += bytes.size();
    return open_;
  }

  void wait_readable(std::chrono::milliseconds /*max*/) noexcept override {}

  /// Every byte handed to `write`, whether or not the line was open.
  [[nodiscard]] std::size_t bytes_written() const noexcept { return bytes_written_; }

 private:
  bool open_;
  std::size_t bytes_written_{0};
};

}  // namespace pigeon::test_support
