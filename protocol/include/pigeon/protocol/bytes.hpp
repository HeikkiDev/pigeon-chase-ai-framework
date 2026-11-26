#pragma once

#include <cstddef>
#include <cstdint>

namespace pigeon::protocol {

/// A borrowed, read-only run of bytes.
///
/// The codec reads untrusted bytes through nothing else. The one property that
/// matters is that an out-of-range access is **answered, never performed**:
/// `at()` returns zero past the end rather than reading beyond the buffer, so
/// no input, however malformed, can make the codec read out of bounds
/// (`REQ-COM-001`: "malformed input is rejected without undefined behaviour").
///
/// A zero is a legitimate byte, so a caller must not use `at()`'s answer to
/// decide whether an index was valid; it checks `size()` for that. The zero is
/// only what keeps a defect from becoming a memory error.
///
/// The view does not own its bytes, which must outlive it. It is a pointer and
/// a length rather than `std::span` because the firmware toolchain is not
/// chosen and `<span>` is C++20 (ADR-0021 D3).
class ByteView {
 public:
  constexpr ByteView() noexcept = default;

  /// A null pointer, or a zero size, makes an empty view: it is never
  /// dereferenced and cannot claim bytes it does not have.
  constexpr ByteView(const std::uint8_t* data, std::size_t size) noexcept
      : data_(size == 0 ? nullptr : data), size_(data == nullptr ? 0 : size) {}

  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

  /// The byte at `index`, or zero when `index` is out of range.
  [[nodiscard]] constexpr std::uint8_t at(std::size_t index) const noexcept {
    // The bound is checked on this line; it is the single place the codec reads
    // through a raw pointer, so that no other place has to.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return index < size_ ? data_[index] : std::uint8_t{0};
  }

  /// The run `[offset, offset + count)`, clipped to this view. A run that
  /// starts past the end is empty; neither argument can name a byte outside it.
  [[nodiscard]] constexpr ByteView subview(std::size_t offset, std::size_t count) const noexcept {
    if (offset >= size_) {
      return ByteView{};
    }
    const std::size_t remaining = size_ - offset;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return ByteView{data_ + offset, count < remaining ? count : remaining};
  }

 private:
  const std::uint8_t* data_{nullptr};
  std::size_t size_{0};
};

/// A borrowed, writable run of bytes: the buffer a caller hands an encoder.
///
/// A write past the end is refused and changes nothing, so an encoder given a
/// buffer that is too small cannot overrun it; it learns of the shortfall from
/// the `false` and reports failure.
class MutableBytes {
 public:
  constexpr MutableBytes() noexcept = default;

  /// As for `ByteView`: a null pointer, or a zero size, makes an empty buffer.
  constexpr MutableBytes(std::uint8_t* data, std::size_t size) noexcept
      : data_(size == 0 ? nullptr : data), size_(data == nullptr ? 0 : size) {}

  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

  /// Writes `value` at `index` and returns true, or returns false and writes
  /// nothing when `index` is out of range.
  [[nodiscard]] constexpr bool set(std::size_t index, std::uint8_t value) noexcept {
    if (index >= size_) {
      return false;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    data_[index] = value;
    return true;
  }

  /// The byte at `index`, or zero when `index` is out of range.
  [[nodiscard]] constexpr std::uint8_t at(std::size_t index) const noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return index < size_ ? data_[index] : std::uint8_t{0};
  }

  /// The first `count` bytes, read-only, clipped to the buffer: what an
  /// encoder has written, handed on to whatever sends it.
  [[nodiscard]] constexpr ByteView view(std::size_t count) const noexcept {
    return ByteView{data_, count < size_ ? count : size_};
  }

 private:
  std::uint8_t* data_{nullptr};
  std::size_t size_{0};
};

}  // namespace pigeon::protocol
