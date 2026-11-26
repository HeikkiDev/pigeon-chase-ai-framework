// Verifies: REQ-DET-001, REQ-DET-004, REQ-AIM-001, REQ-DEV-002
//
// The inverse mapping of ADR-0019 §6: the model answers in normalised
// coordinates, those are clamped to [0, 1] and scaled by the frame's own
// width and height, and the centroid is the centre of the resulting box.
//
// The clamp is not directly observable from outside — nothing in this suite
// can make the model predict a box off the edge on demand — so it is asserted
// as the property it exists to guarantee: **no detection ever leaves the
// frame**. A centroid outside the image is an aim outside the frame's angular
// extent (`REQ-AIM-001`), which is water pointed somewhere nobody authorised.
//
// Frames are 640 x 480, the capture resolution of maintainer ruling Q24.

#include <string>

#include <gtest/gtest.h>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/raspberry/ncnn_detector.hpp"
#include "support/model_fixture.hpp"
#include "support/ppm_fixture.hpp"

namespace {

using pigeon::core::area_px2;
using pigeon::core::Detection;
using pigeon::core::DetectionOutcome;
using pigeon::core::DetectionResult;
using pigeon::raspberry::ModelLoadStatus;
using pigeon::raspberry::NcnnDetector;
using pigeon::test_support::calibrated_settings;
using pigeon::test_support::fixture_path;
using pigeon::test_support::load_ppm;
using pigeon::test_support::PpmImage;

class DetectionGeometryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    result_ = NcnnDetector::load(calibrated_settings());
    ASSERT_EQ(result_.status, ModelLoadStatus::OK);
    ASSERT_NE(result_.detector, nullptr);

    bird_ = load_ppm(fixture_path("pigeon_640x480.ppm"));
    ASSERT_TRUE(bird_.loaded()) << "fixture missing: pigeon_640x480.ppm";
    ASSERT_EQ(bird_.size.width_px, 640U) << "the capture resolution of Q24";
    ASSERT_EQ(bird_.size.height_px, 480U) << "the capture resolution of Q24";

    outcome_ = result_.detector->detect(bird_.view());
    ASSERT_EQ(outcome_.result(), DetectionResult::FOUND);
    ASSERT_FALSE(outcome_.detections().empty());
  }

  [[nodiscard]] const DetectionOutcome& outcome() const { return outcome_; }
  [[nodiscard]] const PpmImage& bird() const { return bird_; }

 private:
  NcnnDetector::LoadResult result_;
  PpmImage bird_;
  DetectionOutcome outcome_;
};

// Encodes ADR-0019 §6: boxes are clamped before scaling "so that a box
// predicted off the edge cannot produce a centroid outside the image".
// Verifies: REQ-DET-004, REQ-AIM-001.
TEST_F(DetectionGeometryTest, EveryBoundingBoxLiesInsideTheFrame) {
  const auto width = static_cast<double>(bird().size.width_px);
  const auto height = static_cast<double>(bird().size.height_px);

  for (const Detection& detection : outcome().detections()) {
    EXPECT_GE(detection.bounding_box_px.top_left_px.x_px, 0.0);
    EXPECT_GE(detection.bounding_box_px.top_left_px.y_px, 0.0);
    EXPECT_LE(detection.bounding_box_px.top_left_px.x_px + detection.bounding_box_px.width_px,
              width)
        << "A box clamped to [0, 1] before scaling cannot extend past the frame.";
    EXPECT_LE(detection.bounding_box_px.top_left_px.y_px + detection.bounding_box_px.height_px,
              height)
        << "A box clamped to [0, 1] before scaling cannot extend past the frame.";
  }
}

// Encodes ADR-0019 §6 and REQ-AIM-001: the centroid is what aiming consumes,
// and an aim point outside the frame has no angular interpretation.
TEST_F(DetectionGeometryTest, EveryCentroidLiesInsideTheFrame) {
  const auto width = static_cast<double>(bird().size.width_px);
  const auto height = static_cast<double>(bird().size.height_px);

  for (const Detection& detection : outcome().detections()) {
    EXPECT_GE(detection.centroid_px.x_px, 0.0);
    EXPECT_LE(detection.centroid_px.x_px, width);
    EXPECT_GE(detection.centroid_px.y_px, 0.0);
    EXPECT_LE(detection.centroid_px.y_px, height);
  }
}

// Encodes ADR-0019 §6: "centroid_px = centre of the box". The two travel
// together into aiming and selection, so a detection whose centroid is not its
// box's centre aims at one thing and sizes another.
// Verifies: REQ-DET-001, REQ-AIM-001.
TEST_F(DetectionGeometryTest, CentroidIsTheCentreOfItsBoundingBox) {
  for (const Detection& detection : outcome().detections()) {
    EXPECT_DOUBLE_EQ(detection.centroid_px.x_px, detection.bounding_box_px.top_left_px.x_px +
                                                     (detection.bounding_box_px.width_px / 2.0));
    EXPECT_DOUBLE_EQ(detection.centroid_px.y_px, detection.bounding_box_px.top_left_px.y_px +
                                                     (detection.bounding_box_px.height_px / 2.0));
  }
}

// Encodes ADR-0019 §6: the box is `(x2 - x1) * width` by `(y2 - y1) * height`,
// so a real detection has a real extent. Single-target selection orders by
// bounding-box area, and a degenerate box makes that order meaningless.
// Verifies: REQ-DET-001, REQ-DET-004.
TEST_F(DetectionGeometryTest, BoundingBoxHasAPositiveExtent) {
  for (const Detection& detection : outcome().detections()) {
    EXPECT_GT(detection.bounding_box_px.width_px, 0.0);
    EXPECT_GT(detection.bounding_box_px.height_px, 0.0);
    EXPECT_GT(area_px2(detection.bounding_box_px), 0.0);
  }
}

// Encodes ADR-0019 §6: the mapping scales by *this frame's* pixel dimensions,
// not by the model's 320 x 320 input. A detector that forgot the inverse
// transform would report coordinates inside 320 x 320 and still pass every
// containment assertion above on a 640 x 480 frame.
// Verifies: REQ-DET-004, REQ-AIM-001.
TEST_F(DetectionGeometryTest, BoxesAreScaledToTheFrameAndNotToTheModelInput) {
  const Detection& detection = outcome().detections().front();
  const double right_edge =
      detection.bounding_box_px.top_left_px.x_px + detection.bounding_box_px.width_px;

  EXPECT_GT(right_edge, 320.0)
      << "The bird spans past the middle of this 640-pixel-wide frame, so a box "
         "that stops inside 320 px was never mapped out of the model's input space.";
}

}  // namespace
