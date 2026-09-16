#pragma once

#include <cstdint>

/// Image-plane geometry: the vocabulary types every detection, track and
/// aiming calculation is expressed in.
///
/// Coordinate frame (ADR-0004, `docs/architecture/architecture.md`):
/// the image frame has its origin at the **top-left** corner, +x to the right
/// and +y **downwards**, as every image library produces it. The servo frame
/// (`aiming.hpp`) is right-handed about a different axis, so the transform
/// between the two flips the sign of the vertical term. Getting that backwards
/// aims water at the ground, which is why both frames are written down.
namespace pigeon::core {

/// A point in the image frame, in pixels.
///
/// Sub-pixel values are meaningful: a centroid is an average, not a sample.
/// Serves `REQ-DET-001`, `REQ-TRK-007`, `REQ-AIM-001`.
struct PixelPoint {
  /// Horizontal position in pixels, increasing to the right of the image.
  double x_px{};
  /// Vertical position in pixels, increasing **downwards** from the top edge.
  double y_px{};

  [[nodiscard]] constexpr bool operator==(const PixelPoint&) const = default;
};

/// An axis-aligned bounding box in the image frame, in pixels.
///
/// The box is described by its top-left corner and its extent, both in the
/// image frame. `REQ-TRK-008` selects a target by bounding-box area, so the
/// extent is part of the domain vocabulary rather than a rendering detail.
struct BoundingBox {
  /// Top-left corner of the box, in the image frame.
  PixelPoint top_left_px{};
  /// Width in pixels. Never negative; a zero width means a degenerate box.
  double width_px{};
  /// Height in pixels. Never negative; a zero height means a degenerate box.
  double height_px{};

  [[nodiscard]] constexpr bool operator==(const BoundingBox&) const = default;
};

/// The pixel dimensions of a frame.
///
/// Required by the aiming transform, which expresses a target's position as a
/// fraction of the frame width and height (`REQ-AIM-001`).
struct ImageSize {
  /// Frame width in pixels. Zero means "no image"; callers must handle it.
  std::uint32_t width_px{};
  /// Frame height in pixels. Zero means "no image"; callers must handle it.
  std::uint32_t height_px{};

  [[nodiscard]] constexpr bool operator==(const ImageSize&) const = default;
};

/// A distance in the image plane, in pixels.
///
/// A distinct type because the association radius (`REQ-TRK-007`) is a length
/// in the image plane and must never be confused with an angle or a duration.
struct PixelDistance {
  /// Magnitude in pixels. Never negative.
  double pixels{};

  [[nodiscard]] constexpr auto operator<=>(const PixelDistance&) const = default;
};

/// Area of a bounding box, in square pixels.
///
/// The ordering key for single-target selection (`REQ-TRK-008`). A degenerate
/// box has zero area.
[[nodiscard]] double area_px2(const BoundingBox& box) noexcept;

/// Centroid-to-centroid distance between two points in the image frame.
///
/// The association metric of `REQ-TRK-007`: a detection joins the nearest
/// existing track whose centroid lies within the association radius.
[[nodiscard]] PixelDistance distance_between(const PixelPoint& first,
                                             const PixelPoint& second) noexcept;

}  // namespace pigeon::core
