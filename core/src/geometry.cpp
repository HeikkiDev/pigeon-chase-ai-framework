#include "pigeon/core/geometry.hpp"

#include <cmath>

namespace pigeon::core {

double area_px2(const BoundingBox& box) noexcept { return box.width_px * box.height_px; }

PixelDistance distance_between(const PixelPoint& first, const PixelPoint& second) noexcept {
  // std::hypot rather than sqrt of a sum of squares: it is exact for the
  // axis-aligned cases the association boundary is asserted on (`REQ-TRK-007`)
  // and does not overflow on the way.
  return PixelDistance{std::hypot(second.x_px - first.x_px, second.y_px - first.y_px)};
}

}  // namespace pigeon::core
