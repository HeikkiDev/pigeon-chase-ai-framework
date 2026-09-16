// Verifies: REQ-TRK-003, REQ-TRK-007, REQ-TRK-009, REQ-DEV-002
//
// Association is what makes confirmation a statement about a bird rather than
// about a frame, so every rule in `REQ-TRK-007` is a safety rule once removed.

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/core/track.hpp"
#include "support/detection_builders.hpp"

namespace {

using pigeon::core::associate_detections;
using pigeon::core::Detection;
using pigeon::core::DetectionOutcome;
using pigeon::core::distance_between;
using pigeon::core::PixelDistance;
using pigeon::core::PixelPoint;
using pigeon::core::Track;
using pigeon::core::TrackId;
using pigeon::core::TrackUpdate;
using pigeon::test_support::detection_at;
using pigeon::test_support::frame_with;

constexpr PixelDistance radius{30.0};
constexpr TrackId first_id{0};

// Verifies: REQ-TRK-007 — "a detection that matches no track starts a new
// track", taking the next identifier.
TEST(Association, AnUnmatchedDetectionStartsANewTrack) {
  const TrackUpdate update =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);

  ASSERT_EQ(update.tracks.size(), 1U);
  EXPECT_EQ(update.tracks.at(0).id, first_id);
  EXPECT_EQ(update.tracks.at(0).consecutive_detections, 1U);
  EXPECT_EQ(update.tracks.at(0).latest, detection_at(100.0, 100.0));
  EXPECT_GT(update.next_id, first_id) << "the identity allocator must move forward";
}

// Verifies: REQ-TRK-007 — "two detections in successive frames within the
// association radius produce one track with a count of two".
TEST(Association, TwoDetectionsWithinTheRadiusProduceOneTrackWithCountTwo) {
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);
  const TrackUpdate second = associate_detections(
      first.tracks, frame_with({detection_at(110.0, 100.0)}), radius, first.next_id);

  ASSERT_EQ(second.tracks.size(), 1U);
  EXPECT_EQ(second.tracks.at(0).id, first.tracks.at(0).id) << "the same bird must keep its identity";
  EXPECT_EQ(second.tracks.at(0).consecutive_detections, 2U);
  EXPECT_EQ(second.tracks.at(0).latest, detection_at(110.0, 100.0))
      << "the track must carry its most recent position, which is what aiming uses";
}

// Verifies: REQ-TRK-007 — "a detection in the following frame that lies beyond
// the association radius from every existing track starts a **new** track with
// a count of one, while the track it failed to match is discarded because it
// received no detection (REQ-TRK-009)".
//
// This is the bullet as reworded by correction C1. The two tracks exist across
// the run rather than simultaneously, so the discard is asserted directly: the
// earlier identifier is gone from the set, not merely outnumbered by a newer
// one.
TEST(Association, ADetectionBeyondTheRadiusStartsANewTrackAndTheOldOneIsDiscarded) {
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);
  ASSERT_EQ(first.tracks.size(), 1U);
  const TrackId abandoned_id = first.tracks.at(0).id;

  const TrackUpdate second = associate_detections(
      first.tracks, frame_with({detection_at(300.0, 100.0)}), radius, first.next_id);

  ASSERT_EQ(second.tracks.size(), 1U) << "the unmatched track must not survive the frame";
  EXPECT_NE(second.tracks.at(0).id, abandoned_id)
      << "a detection outside the radius is a different bird and must not inherit a count";
  EXPECT_EQ(second.tracks.at(0).consecutive_detections, 1U);
  EXPECT_TRUE(std::ranges::none_of(
      second.tracks, [abandoned_id](const Track& track) { return track.id == abandoned_id; }))
      << "the track that received no detection is discarded (REQ-TRK-009)";
}

// Verifies: REQ-TRK-007 — "a detection whose centroid lies at **exactly** the
// association radius from an existing track's centroid joins that track,
// raising its count to two; one a fraction of a pixel further away does not".
//
// The radius is inclusive: the test is `distance <= association_radius`
// (track.hpp, Q17). This lands *on* the boundary rather than straddling it,
// because a `<` written where `<=` was meant is invisible to any pair of
// samples that brackets the value without touching it.
//
// Both offsets are checked against `distance_between` before they are used, so
// the test cannot pass by accident on a distance that is not the one it claims
// to be exercising. 30 px along a single axis is exactly representable and
// `sqrt(900.0)` is exactly 30.0, so "exactly the radius" really is exact here.
TEST(Association, AssociatesAtExactlyTheRadiusAndNotABitBeyondIt) {
  constexpr double origin_x = 100.0;
  constexpr double origin_y = 100.0;
  const PixelPoint centroid{.x_px = origin_x, .y_px = origin_y};

  const PixelPoint on_boundary{.x_px = origin_x + radius.pixels, .y_px = origin_y};
  const PixelPoint just_beyond{.x_px = origin_x + radius.pixels + 1e-9, .y_px = origin_y};

  // Anti-vacuity: prove the fixtures sit where the test says they sit.
  ASSERT_DOUBLE_EQ(distance_between(centroid, on_boundary).pixels, radius.pixels)
      << "the boundary fixture must be exactly the radius away, or this test proves nothing";
  ASSERT_GT(distance_between(centroid, just_beyond).pixels, radius.pixels)
      << "the outside fixture must really be outside";

  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(origin_x, origin_y)}), radius, first_id);
  ASSERT_EQ(first.tracks.size(), 1U);

  const TrackUpdate at_boundary = associate_detections(
      first.tracks, frame_with({detection_at(on_boundary.x_px, on_boundary.y_px)}), radius,
      first.next_id);
  ASSERT_EQ(at_boundary.tracks.size(), 1U);
  EXPECT_EQ(at_boundary.tracks.at(0).id, first.tracks.at(0).id)
      << "the boundary belongs to the track: a centroid exactly the radius away associates";
  EXPECT_EQ(at_boundary.tracks.at(0).consecutive_detections, 2U)
      << "exactly the association radius must raise the count to two, not start a new track";

  const TrackUpdate beyond_boundary = associate_detections(
      first.tracks, frame_with({detection_at(just_beyond.x_px, just_beyond.y_px)}), radius,
      first.next_id);
  ASSERT_EQ(beyond_boundary.tracks.size(), 1U);
  EXPECT_NE(beyond_boundary.tracks.at(0).id, first.tracks.at(0).id)
      << "a fraction of a pixel beyond the radius is a different bird";
  EXPECT_EQ(beyond_boundary.tracks.at(0).consecutive_detections, 1U);
}

// Verifies: REQ-TRK-007 — the same inclusive rule, read at the degenerate end
// of its range. A zero radius admits exactly one distance, zero, and admits it
// (`distance <= radius`), which is what "matches nothing but a detection
// exactly on a track's centroid" in track.hpp means.
//
// Asserted so that a zero radius is not implemented as a special case that
// short-circuits the comparison: the rule is one comparison, everywhere.
TEST(Association, AZeroRadiusStillAssociatesADetectionExactlyOnTheCentroid) {
  constexpr PixelDistance zero_radius{0.0};
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), zero_radius, first_id);
  ASSERT_EQ(first.tracks.size(), 1U);

  const TrackUpdate second = associate_detections(
      first.tracks, frame_with({detection_at(100.0, 100.0)}), zero_radius, first.next_id);

  ASSERT_EQ(second.tracks.size(), 1U);
  EXPECT_EQ(second.tracks.at(0).id, first.tracks.at(0).id);
  EXPECT_EQ(second.tracks.at(0).consecutive_detections, 2U)
      << "distance 0 <= radius 0: the inclusive rule holds at the degenerate boundary too";
}

// Verifies: REQ-TRK-007 — "each detection joins the **nearest** existing track
// whose centroid lies within the radius", not merely the first one in range.
TEST(Association, ADetectionJoinsTheNearestTrackInRange) {
  const TrackUpdate seed = associate_detections(
      {}, frame_with({detection_at(100.0, 100.0), detection_at(120.0, 100.0)}), radius, first_id);
  ASSERT_EQ(seed.tracks.size(), 2U);

  const auto nearer = std::ranges::find_if(seed.tracks, [](const Track& track) {
    return track.latest.centroid_px.x_px == 120.0;
  });
  ASSERT_NE(nearer, seed.tracks.end());
  const TrackId expected_id = nearer->id;

  const TrackUpdate update = associate_detections(
      seed.tracks, frame_with({detection_at(115.0, 100.0)}), radius, seed.next_id);

  ASSERT_EQ(update.tracks.size(), 1U);
  EXPECT_EQ(update.tracks.at(0).id, expected_id)
      << "the detection was 5 px from one track and 15 px from the other";
  EXPECT_EQ(update.tracks.at(0).consecutive_detections, 2U);
}

// Verifies: REQ-TRK-003, REQ-TRK-009 — "a NONE frame empties the track set
// entirely" (track.hpp), which is how the counter reset is realised.
TEST(Association, ANoneFrameEmptiesTheTrackSet) {
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);

  const TrackUpdate second =
      associate_detections(first.tracks, DetectionOutcome::none(), radius, first.next_id);

  EXPECT_TRUE(second.tracks.empty());
}

// Verifies: REQ-TRK-009 — "a track absent for one frame and detected again in
// the next has a consecutive-detection count of one, not two".
TEST(Association, ATrackAbsentForOneFrameRestartsAtOne) {
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);
  const TrackUpdate second =
      associate_detections(first.tracks, frame_with({detection_at(105.0, 100.0)}), radius,
                           first.next_id);
  ASSERT_EQ(second.tracks.at(0).consecutive_detections, 2U);

  const TrackUpdate missed =
      associate_detections(second.tracks, DetectionOutcome::none(), radius, second.next_id);
  const TrackUpdate returned = associate_detections(
      missed.tracks, frame_with({detection_at(105.0, 100.0)}), radius, missed.next_id);

  ASSERT_EQ(returned.tracks.size(), 1U);
  EXPECT_EQ(returned.tracks.at(0).consecutive_detections, 1U)
      << "a bird that vanished for a frame must earn its confirmation again";
  EXPECT_NE(returned.tracks.at(0).id, second.tracks.at(0).id)
      << "there is no retention: the returning bird is a new track (ADR-0010)";
}

// Verifies: REQ-TRK-009 — "every track in the track set has a
// consecutive-detection count of at least one".
TEST(Association, EveryTrackInTheSetHasBeenDetectedInTheFrameJustProcessed) {
  TrackUpdate update;
  update.next_id = first_id;
  const std::vector<std::vector<Detection>> frames{
      {detection_at(10.0, 10.0), detection_at(200.0, 200.0)},
      {detection_at(12.0, 10.0)},
      {detection_at(14.0, 10.0), detection_at(400.0, 50.0), detection_at(200.0, 205.0)},
      {detection_at(600.0, 400.0)},
  };

  for (const std::vector<Detection>& detections : frames) {
    update = associate_detections(update.tracks, frame_with(detections), radius, update.next_id);
    EXPECT_EQ(update.tracks.size(), detections.size())
        << "the surviving set must be exactly the tracks detected in this frame";
    for (const Track& track : update.tracks) {
      EXPECT_GE(track.consecutive_detections, 1U);
    }
  }
}

// Verifies: REQ-SAF-004, REQ-TRK-007 — "a zero association_radius matches
// nothing but a detection exactly on a track's centroid, which no real
// detector produces twice, so the default of an uncalibrated configuration
// starts a new track for every detection and confirms nothing" (track.hpp):
// the inert behaviour of an uncalibrated rig.
//
// The bird therefore moves by a fraction of a pixel between frames, as a real
// one does. The exactly-stationary case is the boundary and is asserted
// separately, above, because under the inclusive rule it associates.
TEST(Association, AZeroRadiusAssociatesNothingAsSoonAsTheBirdMoves) {
  TrackUpdate update;
  update.next_id = first_id;

  for (int frame = 0; frame < 5; ++frame) {
    const double drift = 0.25 * static_cast<double>(frame);
    update = associate_detections(update.tracks, frame_with({detection_at(100.0 + drift, 100.0)}),
                                  PixelDistance{0.0}, update.next_id);
    ASSERT_EQ(update.tracks.size(), 1U);
    EXPECT_EQ(update.tracks.at(0).consecutive_detections, 1U)
        << "an uncalibrated radius must never accumulate a confirmation";
  }
}

// Verifies: REQ-TRK-007, REQ-DEV-002 — "association is deterministic:
// identical frame sequences produce identical tracks, independent of iteration
// order". The detector's ordering is its own business (`detection.hpp`), so a
// permutation of the same detections must produce the same track set,
// identifiers included.
//
// The permutations are drawn from a generator seeded with a fixed constant, so
// the case set is the same on every run.
TEST(Association, IsIndependentOfTheOrderTheDetectorReportsDetectionsIn) {
  constexpr std::uint_fast32_t seed = 20260916;
  const std::vector<Detection> detections{
      detection_at(50.0, 50.0), detection_at(300.0, 120.0), detection_at(80.0, 400.0),
      detection_at(500.0, 300.0), detection_at(55.0, 240.0)};

  const TrackUpdate reference = associate_detections({}, frame_with(detections), radius, first_id);

  std::mt19937 generator{seed};
  std::vector<Detection> permuted = detections;
  for (int attempt = 0; attempt < 20; ++attempt) {
    std::shuffle(permuted.begin(), permuted.end(), generator);
    const TrackUpdate update = associate_detections({}, frame_with(permuted), radius, first_id);

    ASSERT_EQ(update.tracks.size(), reference.tracks.size());
    EXPECT_EQ(update.tracks, reference.tracks)
        << "attempt " << attempt << " (seed " << seed
        << "): the track set depended on the detector's ordering";
    EXPECT_EQ(update.next_id, reference.next_id);
  }
}

// Verifies: REQ-DEV-002 — association is a pure function: same arguments, same
// result, always.
TEST(Association, IsAPureFunctionOfItsArguments) {
  const TrackUpdate seed = associate_detections(
      {}, frame_with({detection_at(100.0, 100.0), detection_at(400.0, 300.0)}), radius, first_id);

  const TrackUpdate first = associate_detections(
      seed.tracks, frame_with({detection_at(105.0, 100.0)}), radius, seed.next_id);
  const TrackUpdate second = associate_detections(
      seed.tracks, frame_with({detection_at(105.0, 100.0)}), radius, seed.next_id);

  EXPECT_EQ(first.tracks, second.tracks);
  EXPECT_EQ(first.next_id, second.next_id);
}

// Verifies: REQ-DEV-002 — "identifiers are assigned deterministically in
// ascending order and are never reused within a run" (track.hpp), which is
// what makes a replayed scenario reproducible.
TEST(Association, IdentifiersAreAscendingAndNeverReusedWithinARun) {
  TrackUpdate update;
  update.next_id = first_id;
  std::vector<TrackId> seen;
  TrackId previous_next = first_id;

  for (int frame = 0; frame < 6; ++frame) {
    // Every frame is a new bird somewhere else, so every frame allocates.
    const double x_px = 20.0 + (static_cast<double>(frame) * 100.0);
    update = associate_detections(update.tracks, frame_with({detection_at(x_px, 30.0)}), radius,
                                  update.next_id);
    ASSERT_EQ(update.tracks.size(), 1U);
    EXPECT_EQ(std::ranges::count(seen, update.tracks.at(0).id), 0)
        << "identifier reused within a run at frame " << frame;
    seen.push_back(update.tracks.at(0).id);
    EXPECT_GE(update.next_id, previous_next) << "the allocator moved backwards";
    previous_next = update.next_id;
  }
}

}  // namespace
