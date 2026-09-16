#pragma once

// Verifies: support code for REQ-TRK-002, REQ-TRK-007, REQ-TRK-008,
// REQ-AIM-001 (builders that keep the intent of a scenario readable).

#include <cstdint>
#include <vector>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/core/track.hpp"

namespace pigeon::test_support {

/// A detection centred on (`centre_x_px`, `centre_y_px`) with the given
/// extent. Centroid and bounding box are kept consistent, because
/// `REQ-TRK-007` associates on the centroid while `REQ-TRK-008` orders on the
/// box, and a scenario in which those two disagree tests nothing anybody
/// meant.
[[nodiscard]] constexpr pigeon::core::Detection detection_at(double centre_x_px,
                                                             double centre_y_px,
                                                             double width_px = 10.0,
                                                             double height_px = 10.0) {
  return pigeon::core::Detection{
      .centroid_px = pigeon::core::PixelPoint{centre_x_px, centre_y_px},
      .bounding_box_px =
          pigeon::core::BoundingBox{
              .top_left_px =
                  pigeon::core::PixelPoint{centre_x_px - (width_px / 2.0),
                                           centre_y_px - (height_px / 2.0)},
              .width_px = width_px,
              .height_px = height_px,
          },
  };
}

/// A track with an explicit consecutive-detection count, for tests that need
/// to start from a state rather than drive the machine into it.
[[nodiscard]] constexpr pigeon::core::Track track_with(std::uint32_t id,
                                                       pigeon::core::Detection latest,
                                                       std::uint32_t consecutive_detections) {
  return pigeon::core::Track{
      .id = static_cast<pigeon::core::TrackId>(id),
      .latest = latest,
      .consecutive_detections = consecutive_detections,
  };
}

/// One frame's detections, in the detector's own order.
[[nodiscard]] inline pigeon::core::DetectionOutcome frame_with(
    std::vector<pigeon::core::Detection> detections) {
  return pigeon::core::DetectionOutcome::found(std::move(detections));
}

}  // namespace pigeon::test_support
