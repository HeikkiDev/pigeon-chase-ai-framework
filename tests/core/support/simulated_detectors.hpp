#pragma once

// Verifies: REQ-DET-001, REQ-DET-002, REQ-DEV-002 (support code — the two
// simulated detectors the default suite runs against, in place of a camera and
// a model runtime).

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/detector.hpp"
#include "pigeon/core/frame.hpp"
#include "pigeon/core/geometry.hpp"

namespace pigeon::test_support {

/// A detector that finds bright rectangles in an RGB888 fixture.
///
/// It is deliberately trivial: the point of `REQ-DET-002` is that the domain
/// depends on the *interface*, so the default suite can drive the whole
/// detection path with no camera, no model runtime and no platform header. It
/// is also pure — the same frame always yields the same outcome, which is what
/// `REQ-DEV-002` requires of a detector and what `Detector::detect` being
/// `const` is there to make hard to violate.
///
/// Reads pixels through the addressing `REQ-DET-004` documents:
/// `y * stride_bytes + x * bytes_per_pixel`.
class BrightBlobDetector final : public pigeon::core::Detector {
 public:
  /// Red-channel level at or above which a pixel counts as pigeon.
  static constexpr std::uint8_t threshold = 0x80;

  [[nodiscard]] pigeon::core::DetectionOutcome detect(
      const pigeon::core::FrameView& frame) const override {
    // A malformed or absent frame is NONE, never an error: inaction is the
    // safe response (`detector.hpp`, `REQ-DET-001`).
    const std::size_t minimum_stride =
        static_cast<std::size_t>(frame.size.width_px) * pigeon::core::bytes_per_pixel;
    if (frame.size.width_px == 0 || frame.size.height_px == 0 ||
        frame.stride_bytes < minimum_stride ||
        frame.pixels.size() < frame.stride_bytes * static_cast<std::size_t>(frame.size.height_px)) {
      return pigeon::core::DetectionOutcome::none();
    }

    const std::size_t width = frame.size.width_px;
    const std::size_t height = frame.size.height_px;
    std::vector<char> visited(width * height, 0);
    std::vector<pigeon::core::Detection> detections;

    for (std::size_t y = 0; y < height; ++y) {
      for (std::size_t x = 0; x < width; ++x) {
        if (visited[(y * width) + x] != 0 || !is_pigeon_pixel(frame, x, y)) {
          continue;
        }
        detections.push_back(grow_blob(frame, visited, x, y));
      }
    }

    if (detections.empty()) {
      return pigeon::core::DetectionOutcome::none();
    }
    return pigeon::core::DetectionOutcome::found(std::move(detections));
  }

 private:
  [[nodiscard]] static bool is_pigeon_pixel(const pigeon::core::FrameView& frame, std::size_t x,
                                            std::size_t y) {
    const std::size_t offset = (y * frame.stride_bytes) + (x * pigeon::core::bytes_per_pixel);
    return std::to_integer<std::uint8_t>(frame.pixels[offset]) >= threshold;
  }

  /// Four-neighbour flood fill from a seed pixel, in a fixed order, so the
  /// detections come out in the same order on every run (`REQ-DEV-002`).
  [[nodiscard]] static pigeon::core::Detection grow_blob(const pigeon::core::FrameView& frame,
                                                         std::vector<char>& visited,
                                                         std::size_t seed_x, std::size_t seed_y) {
    const std::size_t width = frame.size.width_px;
    const std::size_t height = frame.size.height_px;

    std::vector<std::pair<std::size_t, std::size_t>> pending{{seed_x, seed_y}};
    visited[(seed_y * width) + seed_x] = 1;

    std::size_t min_x = seed_x;
    std::size_t max_x = seed_x;
    std::size_t min_y = seed_y;
    std::size_t max_y = seed_y;
    std::size_t sum_x = 0;
    std::size_t sum_y = 0;
    std::size_t count = 0;

    while (!pending.empty()) {
      const auto [x, y] = pending.back();
      pending.pop_back();

      min_x = (x < min_x) ? x : min_x;
      max_x = (x > max_x) ? x : max_x;
      min_y = (y < min_y) ? y : min_y;
      max_y = (y > max_y) ? y : max_y;
      sum_x += x;
      sum_y += y;
      ++count;

      const std::pair<std::size_t, std::size_t> neighbours[] = {
          {x + 1, y}, {x, y + 1}, {x == 0 ? x : x - 1, y}, {x, y == 0 ? y : y - 1}};
      for (const auto& [nx, ny] : neighbours) {
        if (nx >= width || ny >= height || visited[(ny * width) + nx] != 0 ||
            !is_pigeon_pixel(frame, nx, ny)) {
          continue;
        }
        visited[(ny * width) + nx] = 1;
        pending.emplace_back(nx, ny);
      }
    }

    // Pixel centres sit at half-integer coordinates, so a blob spanning
    // [min, max] has its centroid at the centre of its bounding box.
    const double centroid_x = (static_cast<double>(sum_x) / static_cast<double>(count)) + 0.5;
    const double centroid_y = (static_cast<double>(sum_y) / static_cast<double>(count)) + 0.5;

    return pigeon::core::Detection{
        .centroid_px = pigeon::core::PixelPoint{centroid_x, centroid_y},
        .bounding_box_px =
            pigeon::core::BoundingBox{
                .top_left_px = pigeon::core::PixelPoint{static_cast<double>(min_x),
                                                        static_cast<double>(min_y)},
                .width_px = static_cast<double>(max_x - min_x + 1),
                .height_px = static_cast<double>(max_y - min_y + 1),
            },
    };
  }
};

/// A detector that replays a recorded scenario, indexed by the frame's
/// sequence number.
///
/// Indexed rather than counted, so it holds no cursor: replaying the same
/// scenario twice gives the same answers in the same order regardless of how
/// the frames are interleaved (`REQ-DEV-002`). Frames beyond the end of the
/// script are `NONE`.
class ScriptedDetector final : public pigeon::core::Detector {
 public:
  explicit ScriptedDetector(std::vector<pigeon::core::DetectionOutcome> script)
      : script_{std::move(script)} {}

  [[nodiscard]] pigeon::core::DetectionOutcome detect(
      const pigeon::core::FrameView& frame) const override {
    if (frame.sequence_number >= script_.size()) {
      return pigeon::core::DetectionOutcome::none();
    }
    return script_[static_cast<std::size_t>(frame.sequence_number)];
  }

 private:
  std::vector<pigeon::core::DetectionOutcome> script_;
};

}  // namespace pigeon::test_support
