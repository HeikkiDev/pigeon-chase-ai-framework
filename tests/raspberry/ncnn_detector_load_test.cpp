// Verifies: REQ-DET-001, REQ-DET-002, REQ-DEV-001, REQ-DEV-002
//
// Loading the real model is the only fallible operation on this seam, and
// ADR-0019 §3 requires it to report failure as a named value rather than by
// throwing across a module boundary. These suites encode that, together with
// the ADR-0008 property the threshold default exists for: an unconfigured rig
// is inert *structurally*, not because somebody remembered to validate it.
//
// Nothing here opens a device or reads a clock: the model and the images come
// from version-controlled files, so this runs on macOS with no Raspberry Pi
// present (`REQ-DEV-001`). The suite is not hardware-in-the-loop, so it does
// not and must not claim the hardware-only requirement either.

#include <string>

#include <gtest/gtest.h>

#include "pigeon/core/detection.hpp"
#include "pigeon/raspberry/ncnn_detector.hpp"
#include "support/model_fixture.hpp"
#include "support/ppm_fixture.hpp"

namespace {

using pigeon::core::DetectionResult;
using pigeon::raspberry::ModelLoadStatus;
using pigeon::raspberry::NcnnDetector;
using pigeon::raspberry::NcnnDetectorSettings;
using pigeon::test_support::calibrated_settings;
using pigeon::test_support::fixture_path;
using pigeon::test_support::load_ppm;
using pigeon::test_support::model_param_path;
using pigeon::test_support::model_weights_path;
using pigeon::test_support::PpmImage;
using pigeon::test_support::uncalibrated_settings;

// ---------------------------------------------------------------------------
// Load outcomes — ADR-0019 §3: failure is a value, never an exception.
// ---------------------------------------------------------------------------

// Encodes ADR-0019 §3: "a LoadResult that nobody filled in is a failure, not a
// working detector" (ADR-0008). Verifies: REQ-DET-001.
TEST(NcnnDetectorLoad, DefaultConstructedResultIsAFailureWithNoDetector) {
  const NcnnDetector::LoadResult result;

  EXPECT_EQ(result.status, ModelLoadStatus::UNCONFIGURED)
      << "A result nobody filled in must not claim a loaded model.";
  EXPECT_EQ(result.detector, nullptr) << "An unfilled result must carry no detector to call.";
}

// Encodes ADR-0019 §3 and §5: default settings name no model, so the load
// reports UNCONFIGURED and hands back nothing. Verifies: REQ-DET-001.
TEST(NcnnDetectorLoad, DefaultSettingsReportUnconfiguredAndYieldNoDetector) {
  const NcnnDetector::LoadResult result = NcnnDetector::load(NcnnDetectorSettings{});

  EXPECT_EQ(result.status, ModelLoadStatus::UNCONFIGURED);
  EXPECT_EQ(result.detector, nullptr) << "An unconfigured rig must not produce a detector at all.";
}

// Encodes ADR-0019 §3: the error is named after the file that is missing, so a
// deployment fault is diagnosable. Verifies: REQ-DET-001.
TEST(NcnnDetectorLoad, MissingParamFileReportsParamNotFound) {
  NcnnDetectorSettings settings = calibrated_settings();
  settings.param_path = fixture_path("no_such_model.param");

  const NcnnDetector::LoadResult result = NcnnDetector::load(settings);

  EXPECT_EQ(result.status, ModelLoadStatus::PARAM_NOT_FOUND);
  EXPECT_EQ(result.detector, nullptr);
}

// Encodes ADR-0019 §3. Verifies: REQ-DET-001.
TEST(NcnnDetectorLoad, MissingWeightsFileReportsWeightsNotFound) {
  NcnnDetectorSettings settings = calibrated_settings();
  settings.weights_path = fixture_path("no_such_model.bin");

  const NcnnDetector::LoadResult result = NcnnDetector::load(settings);

  EXPECT_EQ(result.status, ModelLoadStatus::WEIGHTS_NOT_FOUND);
  EXPECT_EQ(result.detector, nullptr);
}

// Encodes ADR-0019 §3 and §8: a file that exists but is not a model — the
// shape a corrupted or swapped model bundle takes — is distinguished from an
// absent one. Verifies: REQ-DET-001.
TEST(NcnnDetectorLoad, CorruptParamFileReportsMalformed) {
  NcnnDetectorSettings settings = calibrated_settings();
  settings.param_path = fixture_path("malformed_model.param");

  const NcnnDetector::LoadResult result = NcnnDetector::load(settings);

  EXPECT_EQ(result.status, ModelLoadStatus::MALFORMED)
      << "A present-but-unparseable topology file is corrupt, not missing.";
  EXPECT_EQ(result.detector, nullptr);
}

// Encodes ADR-0019 §3 and §8. Verifies: REQ-DET-001.
TEST(NcnnDetectorLoad, CorruptWeightsFileReportsMalformed) {
  NcnnDetectorSettings settings = calibrated_settings();
  settings.weights_path = fixture_path("malformed_model.bin");

  const NcnnDetector::LoadResult result = NcnnDetector::load(settings);

  EXPECT_EQ(result.status, ModelLoadStatus::MALFORMED);
  EXPECT_EQ(result.detector, nullptr);
}

// Encodes ADR-0019 §3: "never an exception across this module boundary". Every
// failure path is exercised through one assertion, because a throw from any of
// them would cross into `core/`'s caller, which has no handler and no safe
// answer. Verifies: REQ-DET-001.
TEST(NcnnDetectorLoad, NoFailurePathThrows) {
  NcnnDetectorSettings missing_param = calibrated_settings();
  missing_param.param_path = fixture_path("no_such_model.param");

  NcnnDetectorSettings missing_weights = calibrated_settings();
  missing_weights.weights_path = fixture_path("no_such_model.bin");

  NcnnDetectorSettings corrupt_param = calibrated_settings();
  corrupt_param.param_path = fixture_path("malformed_model.param");

  NcnnDetectorSettings corrupt_weights = calibrated_settings();
  corrupt_weights.weights_path = fixture_path("malformed_model.bin");

  NcnnDetectorSettings empty_param = calibrated_settings();
  empty_param.param_path.clear();

  EXPECT_NO_THROW({ (void)NcnnDetector::load(NcnnDetectorSettings{}); });
  EXPECT_NO_THROW({ (void)NcnnDetector::load(missing_param); });
  EXPECT_NO_THROW({ (void)NcnnDetector::load(missing_weights); });
  EXPECT_NO_THROW({ (void)NcnnDetector::load(corrupt_param); });
  EXPECT_NO_THROW({ (void)NcnnDetector::load(corrupt_weights); });
  EXPECT_NO_THROW({ (void)NcnnDetector::load(empty_param); });
}

// Encodes ADR-0019 §3 and §8: the vendored bundle really is loadable, which is
// what makes every other assertion in tests/raspberry/ reachable.
// Verifies: REQ-DET-001, REQ-DEV-001.
TEST(NcnnDetectorLoad, VendoredModelLoadsAndYieldsADetector) {
  const NcnnDetector::LoadResult result = NcnnDetector::load(calibrated_settings());

  ASSERT_EQ(result.status, ModelLoadStatus::OK)
      << "The vendored model bundle must load from " << model_param_path() << " and "
      << model_weights_path() << ".";
  EXPECT_NE(result.detector, nullptr) << "An OK status must carry a usable detector.";
}

// ---------------------------------------------------------------------------
// Non-positive thread counts — maintainer ruling Q25, 2026-10-05.
//
// "A `num_threads <= 0` in `NcnnDetectorSettings` is treated as 1; it is not a
// load failure and adds no `ModelLoadStatus`." The ruling has two halves and
// both are asserted below: the load still succeeds, and the detector it hands
// back answers like the one-thread detector it was clamped to. A deployment
// whose configuration file carried a zero would otherwise either fail to start
// or start with ncnn's own machine-derived default, and the second of those is
// exactly the host dependence `REQ-DEV-002` forbids.
// ---------------------------------------------------------------------------

// Encodes Q25: zero threads loads, rather than being rejected as a bad value.
// Verifies: REQ-DET-001, REQ-DEV-002.
TEST(NonPositiveThreadCount, ZeroThreadsStillLoadsADetector) {
  NcnnDetectorSettings settings = calibrated_settings();
  settings.num_threads = 0;

  const NcnnDetector::LoadResult result = NcnnDetector::load(settings);

  EXPECT_EQ(result.status, ModelLoadStatus::OK)
      << "Q25: a thread count of zero is clamped to one, not reported as a load failure.";
  EXPECT_NE(result.detector, nullptr) << "An OK status must carry a usable detector.";
}

// Encodes Q25: the ruling says `<= 0`, not `== 0`, so a negative value — the
// shape a mis-parsed or hand-edited configuration takes — is clamped the same
// way. Verifies: REQ-DET-001, REQ-DEV-002.
TEST(NonPositiveThreadCount, NegativeThreadsStillLoadsADetector) {
  NcnnDetectorSettings settings = calibrated_settings();
  settings.num_threads = -4;

  const NcnnDetector::LoadResult result = NcnnDetector::load(settings);

  EXPECT_EQ(result.status, ModelLoadStatus::OK)
      << "Q25: a negative thread count is clamped to one, not reported as a load failure.";
  EXPECT_NE(result.detector, nullptr) << "An OK status must carry a usable detector.";
}

// Encodes Q25's "treated as 1": loading is only half the ruling. A detector
// that loaded but then ran on some machine-derived thread count would satisfy
// the status assertions above and still be wrong. Only the verdict is
// compared, never a coordinate: ADR-0019 records that the thread count changes
// numerics, so pinning a centroid across thread counts would be a test of this
// Mac. Verifies: REQ-DET-001, REQ-DEV-002.
TEST(NonPositiveThreadCount, ClampedDetectorGivesTheSameVerdictAsOneThread) {
  const PpmImage bird = load_ppm(fixture_path("pigeon_640x480.ppm"));
  const PpmImage wall = load_ppm(fixture_path("brick_wall_320x240.ppm"));
  ASSERT_TRUE(bird.loaded()) << "fixture missing: pigeon_640x480.ppm";
  ASSERT_TRUE(wall.loaded()) << "fixture missing: brick_wall_320x240.ppm";

  const NcnnDetector::LoadResult one_thread = NcnnDetector::load(calibrated_settings());
  ASSERT_EQ(one_thread.status, ModelLoadStatus::OK);
  ASSERT_NE(one_thread.detector, nullptr);

  const DetectionResult bird_verdict = one_thread.detector->detect(bird.view()).result();
  const DetectionResult wall_verdict = one_thread.detector->detect(wall.view()).result();
  ASSERT_EQ(bird_verdict, DetectionResult::FOUND)
      << "The bird fixture must be detectable at one thread, or this test proves nothing.";

  for (const int threads : {0, -1, -8}) {
    NcnnDetectorSettings settings = calibrated_settings();
    settings.num_threads = threads;

    const NcnnDetector::LoadResult clamped = NcnnDetector::load(settings);
    ASSERT_EQ(clamped.status, ModelLoadStatus::OK) << "num_threads = " << threads;
    ASSERT_NE(clamped.detector, nullptr) << "num_threads = " << threads;

    EXPECT_EQ(clamped.detector->detect(bird.view()).result(), bird_verdict)
        << "num_threads = " << threads << " must answer like num_threads = 1.";
    EXPECT_EQ(clamped.detector->detect(wall.view()).result(), wall_verdict)
        << "num_threads = " << threads << " must answer like num_threads = 1.";
  }
}

// ---------------------------------------------------------------------------
// Inert by default — ADR-0008, ADR-0019 §5.
// ---------------------------------------------------------------------------

// Encodes ADR-0019's own verification clause: "a test constructs
// NcnnDetectorSettings{}, loads the real model with it, and asserts NONE for a
// frame that the calibrated detector reports FOUND for". An uncalibrated rig
// does not confirm, does not lock and cannot fire.
// Verifies: REQ-DET-001, REQ-SAF-004.
TEST(UncalibratedNcnnDetector, ReportsNoneForTheFrameACalibratedOneFinds) {
  const PpmImage bird = load_ppm(fixture_path("pigeon_640x480.ppm"));
  ASSERT_TRUE(bird.loaded()) << "fixture missing: pigeon_640x480.ppm";

  const NcnnDetector::LoadResult calibrated = NcnnDetector::load(calibrated_settings());
  ASSERT_EQ(calibrated.status, ModelLoadStatus::OK);
  ASSERT_NE(calibrated.detector, nullptr);
  ASSERT_EQ(calibrated.detector->detect(bird.view()).result(), DetectionResult::FOUND)
      << "The fixture must be detectable, or this test proves nothing.";

  const NcnnDetector::LoadResult uncalibrated = NcnnDetector::load(uncalibrated_settings());
  ASSERT_EQ(uncalibrated.status, ModelLoadStatus::OK)
      << "An uncalibrated threshold is not a load failure: the model is fine.";
  ASSERT_NE(uncalibrated.detector, nullptr);

  EXPECT_EQ(uncalibrated.detector->detect(bird.view()).result(), DetectionResult::NONE)
      << "A threshold above 1.0 is unreachable, so an uncalibrated rig must detect nothing.";
}

// Encodes ADR-0019 §5: inertness is a property of the default, not of one
// lucky image. Verifies: REQ-DET-001, REQ-SAF-004.
TEST(UncalibratedNcnnDetector, ReportsNoneForEveryFixture) {
  const NcnnDetector::LoadResult loaded = NcnnDetector::load(uncalibrated_settings());
  ASSERT_EQ(loaded.status, ModelLoadStatus::OK);
  ASSERT_NE(loaded.detector, nullptr);

  for (const std::string& name :
       {"pigeon_640x480.ppm", "brick_wall_320x240.ppm", "car_320x240.ppm"}) {
    const PpmImage image = load_ppm(fixture_path(name));
    ASSERT_TRUE(image.loaded()) << "fixture missing: " << name;

    EXPECT_EQ(loaded.detector->detect(image.view()).result(), DetectionResult::NONE)
        << "Uncalibrated detector reported a detection for " << name << ".";
  }
}

}  // namespace
