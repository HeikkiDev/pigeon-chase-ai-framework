// Verifies: REQ-TRK-007, REQ-TRK-008
//
// The two image-plane primitives the tracking requirements are written in
// terms of: the association metric (`REQ-TRK-007` — "the nearest existing
// track whose centroid lies within a configured association radius") and the
// selection key (`REQ-TRK-008` — "the detection with the largest bounding-box
// area").

#include <gtest/gtest.h>

#include "pigeon/core/geometry.hpp"

namespace {

using pigeon::core::area_px2;
using pigeon::core::BoundingBox;
using pigeon::core::distance_between;
using pigeon::core::PixelDistance;
using pigeon::core::PixelPoint;

// Verifies: REQ-TRK-008 — selection orders by bounding-box area, so the area
// of a box must be its width times its height and nothing else.
TEST(BoundingBoxArea, IsWidthTimesHeight) {
  const BoundingBox box{.top_left_px = PixelPoint{10.0, 20.0}, .width_px = 4.0, .height_px = 2.5};

  EXPECT_DOUBLE_EQ(area_px2(box), 10.0);
}

// Verifies: REQ-TRK-008 — "a degenerate box has zero area" (geometry.hpp), so
// a zero-extent detection can never outrank a real one.
TEST(BoundingBoxArea, IsZeroForADegenerateBox) {
  const BoundingBox zero_width{
      .top_left_px = PixelPoint{1.0, 1.0}, .width_px = 0.0, .height_px = 9.0};
  const BoundingBox zero_height{
      .top_left_px = PixelPoint{1.0, 1.0}, .width_px = 9.0, .height_px = 0.0};

  EXPECT_DOUBLE_EQ(area_px2(zero_width), 0.0);
  EXPECT_DOUBLE_EQ(area_px2(zero_height), 0.0);
}

// Verifies: REQ-TRK-008 — the area of a box does not depend on where it is,
// only on its extent. A bird in the corner must not be ranked differently from
// the same bird in the centre.
TEST(BoundingBoxArea, DoesNotDependOnPosition) {
  const BoundingBox near_origin{
      .top_left_px = PixelPoint{0.0, 0.0}, .width_px = 6.0, .height_px = 3.0};
  const BoundingBox far_away{
      .top_left_px = PixelPoint{600.0, 400.0}, .width_px = 6.0, .height_px = 3.0};

  EXPECT_DOUBLE_EQ(area_px2(near_origin), area_px2(far_away));
}

// Verifies: REQ-TRK-007 — the association metric is centroid-to-centroid
// distance in the image plane.
TEST(CentroidDistance, IsTheEuclideanDistanceInPixels) {
  const PixelDistance distance =
      distance_between(PixelPoint{0.0, 0.0}, PixelPoint{3.0, 4.0});

  EXPECT_DOUBLE_EQ(distance.pixels, 5.0);
}

// Verifies: REQ-TRK-007 — distance is symmetric, so which of two detections is
// named first cannot change whether they associate.
TEST(CentroidDistance, IsSymmetric) {
  const PixelPoint first{12.5, -3.0};
  const PixelPoint second{-4.5, 8.25};

  EXPECT_EQ(distance_between(first, second), distance_between(second, first));
}

// Verifies: REQ-TRK-007 — a track associates with a detection at its own
// centroid at distance zero, the nearest possible match.
TEST(CentroidDistance, IsZeroForCoincidentPoints) {
  const PixelDistance distance =
      distance_between(PixelPoint{7.5, 7.5}, PixelPoint{7.5, 7.5});

  EXPECT_DOUBLE_EQ(distance.pixels, 0.0);
}

// Verifies: REQ-TRK-007 — distance is never negative, so comparing it against
// a radius is meaningful in both directions.
TEST(CentroidDistance, IsNeverNegative) {
  for (double dx : {-10.0, -1.0, 0.0, 1.0, 10.0}) {
    for (double dy : {-10.0, -1.0, 0.0, 1.0, 10.0}) {
      const PixelDistance distance =
          distance_between(PixelPoint{0.0, 0.0}, PixelPoint{dx, dy});
      EXPECT_GE(distance.pixels, 0.0) << "distance from (0,0) to (" << dx << "," << dy << ")";
    }
  }
}

}  // namespace
