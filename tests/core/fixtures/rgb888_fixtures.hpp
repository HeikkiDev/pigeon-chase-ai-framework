#pragma once

// Verifies: REQ-DET-004 (support code — the RGB888 fixtures every detection
// test is driven from), REQ-DEV-001 (no camera is involved in producing them).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "pigeon/core/frame.hpp"
#include "pigeon/core/geometry.hpp"

namespace pigeon::test_fixtures {

/// A rectangle of "pigeon" pixels, in the image frame: pixels `x0_px` through
/// `x0_px + width_px - 1` inclusive, and likewise vertically.
struct BlobRect {
  std::uint32_t x0_px{};
  std::uint32_t y0_px{};
  std::uint32_t width_px{};
  std::uint32_t height_px{};
};

/// Background and foreground levels. Chosen far apart so that the fixture
/// detector's threshold is not a tuning parameter anybody has to calibrate.
inline constexpr std::uint8_t background_level = 0x10;
inline constexpr std::uint8_t pigeon_level = 0xF0;
inline constexpr std::uint8_t padding_level = 0x00;

/// A fixture frame: the pixel buffer plus the view `core/` is given of it.
///
/// The buffer is owned here and the `FrameView` borrows it (`REQ-DET-002`),
/// which is exactly the ownership split deployment uses — only with a
/// version-controlled generator in place of a camera.
struct FixtureImage {
  std::vector<std::byte> bytes;
  pigeon::core::ImageSize size{};
  std::size_t stride_bytes{};

  [[nodiscard]] pigeon::core::FrameView view(std::uint64_t sequence_number = 0) const {
    return pigeon::core::FrameView{
        .size = size,
        .stride_bytes = stride_bytes,
        .pixels = std::span<const std::byte>{bytes},
        .sequence_number = sequence_number,
    };
  }
};

/// Byte offset of the pixel at (`x_px`, `y_px`), per `REQ-DET-004`:
/// `y * stride + x * bytes_per_pixel`. Written once, here, so that the
/// fixtures and the tests that read them cannot disagree about it.
[[nodiscard]] inline std::size_t pixel_offset(std::size_t stride_bytes, std::uint32_t x_px,
                                              std::uint32_t y_px) {
  return (static_cast<std::size_t>(y_px) * stride_bytes) +
         (static_cast<std::size_t>(x_px) * pigeon::core::bytes_per_pixel);
}

/// Build a packed RGB888 fixture, deterministically: same arguments, same
/// bytes, every run (`REQ-DET-004`, `REQ-DEV-002`).
///
/// `row_padding_bytes` pads every row so that a fixture with
/// `stride > width * 3` can be exercised; the padding carries a distinct value
/// so that a reader which ignores the stride is caught rather than tolerated.
[[nodiscard]] inline FixtureImage make_frame(pigeon::core::ImageSize size,
                                             std::size_t row_padding_bytes,
                                             std::span<const BlobRect> blobs) {
  FixtureImage image;
  image.size = size;
  image.stride_bytes =
      (static_cast<std::size_t>(size.width_px) * pigeon::core::bytes_per_pixel) + row_padding_bytes;
  image.bytes.assign(image.stride_bytes * static_cast<std::size_t>(size.height_px),
                     std::byte{padding_level});

  for (std::uint32_t y_px = 0; y_px < size.height_px; ++y_px) {
    for (std::uint32_t x_px = 0; x_px < size.width_px; ++x_px) {
      const std::size_t offset = pixel_offset(image.stride_bytes, x_px, y_px);
      for (std::size_t channel = 0; channel < pigeon::core::bytes_per_pixel; ++channel) {
        image.bytes[offset + channel] = std::byte{background_level};
      }
    }
  }

  for (const BlobRect& blob : blobs) {
    for (std::uint32_t y_px = blob.y0_px; y_px < blob.y0_px + blob.height_px; ++y_px) {
      for (std::uint32_t x_px = blob.x0_px; x_px < blob.x0_px + blob.width_px; ++x_px) {
        const std::size_t offset = pixel_offset(image.stride_bytes, x_px, y_px);
        for (std::size_t channel = 0; channel < pigeon::core::bytes_per_pixel; ++channel) {
          image.bytes[offset + channel] = std::byte{pigeon_level};
        }
      }
    }
  }

  return image;
}

inline constexpr pigeon::core::ImageSize fixture_size{.width_px = 16, .height_px = 12};

/// The blob in `pigeon_frame()`: pixels x ∈ [4, 7], y ∈ [3, 6].
inline constexpr BlobRect pigeon_blob{.x0_px = 4, .y0_px = 3, .width_px = 4, .height_px = 4};

/// The two blobs in `two_pigeon_frame()`, deliberately of different areas so
/// that `REQ-TRK-008` selection has an unambiguous answer.
inline constexpr BlobRect small_blob{.x0_px = 1, .y0_px = 1, .width_px = 2, .height_px = 2};
inline constexpr BlobRect large_blob{.x0_px = 9, .y0_px = 6, .width_px = 5, .height_px = 4};

/// A frame with no pigeon in it (`REQ-DET-001`).
[[nodiscard]] inline FixtureImage empty_frame() {
  return make_frame(fixture_size, 0, std::span<const BlobRect>{});
}

/// A frame containing one pigeon (`REQ-DET-001`).
[[nodiscard]] inline FixtureImage pigeon_frame() {
  const BlobRect blobs[] = {pigeon_blob};
  return make_frame(fixture_size, 0, std::span<const BlobRect>{blobs});
}

/// The same pigeon, in a frame whose rows are padded (`REQ-DET-004`).
[[nodiscard]] inline FixtureImage padded_pigeon_frame() {
  const BlobRect blobs[] = {pigeon_blob};
  return make_frame(fixture_size, 7, std::span<const BlobRect>{blobs});
}

/// A frame containing two pigeons of different sizes (`REQ-TRK-008`).
[[nodiscard]] inline FixtureImage two_pigeon_frame() {
  const BlobRect blobs[] = {small_blob, large_blob};
  return make_frame(fixture_size, 0, std::span<const BlobRect>{blobs});
}

}  // namespace pigeon::test_fixtures
