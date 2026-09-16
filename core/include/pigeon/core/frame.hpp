#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "pigeon/core/geometry.hpp"

namespace pigeon::core {

/// Bytes per pixel in the declared frame format, RGB888 (`REQ-DET-004`).
inline constexpr std::size_t bytes_per_pixel = 3;

/// A borrowed, read-only view of one camera frame (`REQ-DET-002`).
///
/// This is the whole of `core/`'s knowledge of image acquisition. There is no
/// camera type, no device handle and no capture call anywhere in `core/`: a
/// producer — `raspberry/` in deployment, a fixture loader in `tests/` — owns
/// the pixel buffer and hands a view of it to a `Detector`.
///
/// Ownership and lifetime:
/// * The view **does not own** `pixels`. The buffer must outlive the view.
/// * The buffer is never copied across this boundary. The deployment target
///   has 1 GB of RAM (`docs/architecture/architecture.md`), so a per-frame copy
///   is a budget decision, not a style preference.
///
/// **Pixel format (`REQ-DET-004`): packed RGB888.** Three 8-bit channels per
/// pixel in red, green, blue order; rows ordered top to bottom; the pixel at
/// `(x, y)` begins at byte `y * stride_bytes + x * bytes_per_pixel`. There is
/// deliberately no format field: one declared format keeps every fixture
/// comparable, and a format nobody can vary is a format nobody can get wrong.
/// `core/` performs no colour conversion — a producer that cannot emit RGB888
/// converts on its own side.
struct FrameView {
  /// Frame dimensions in pixels. Also the denominator of the aiming transform
  /// (`REQ-AIM-001`), so it travels with the pixels rather than being
  /// configured separately.
  ImageSize size{};

  /// Distance in bytes between the starts of two consecutive rows. At least
  /// `size.width_px * bytes_per_pixel`; larger when the producer pads rows
  /// (`REQ-DET-004`).
  std::size_t stride_bytes{};

  /// The pixel buffer, borrowed. At least `stride_bytes * size.height_px`
  /// bytes, and empty when no image is available.
  std::span<const std::byte> pixels;

  /// Position of this frame in the capture sequence, assigned by the producer
  /// and increasing by one per captured frame.
  ///
  /// This is the system's only notion of time on the detection path: ADR-0005
  /// counts frames rather than milliseconds so that the target state machine
  /// stays a pure function. `core/` uses it to describe *which* frame it is
  /// talking about, never to measure an interval.
  std::uint64_t sequence_number{};
};

}  // namespace pigeon::core
