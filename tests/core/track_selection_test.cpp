// Verifies: REQ-TRK-002, REQ-TRK-008, REQ-DEV-002
//
// Confirmation is a property of one track, and exactly one confirmed track is
// engaged at a time — chosen by a rule that must give the same answer on every
// run, because pigeons are gregarious and ties are ordinary.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "pigeon/core/geometry.hpp"
#include "pigeon/core/track.hpp"
#include "support/detection_builders.hpp"

namespace {

using pigeon::core::confirmation_frame_count;
using pigeon::core::is_confirmed;
using pigeon::core::select_confirmed_target;
using pigeon::core::Track;
using pigeon::test_support::detection_at;
using pigeon::test_support::track_with;

// Verifies: REQ-TRK-002 — confirmation requires three consecutive detections
// of one track. The constant is part of the specification, not of the rig.
TEST(Confirmation, RequiresThreeConsecutiveDetectionsOfOneTrack) {
  EXPECT_EQ(confirmation_frame_count, 3U);

  EXPECT_FALSE(is_confirmed(track_with(0, detection_at(10.0, 10.0), 1)));
  EXPECT_FALSE(is_confirmed(track_with(0, detection_at(10.0, 10.0), 2)));
  EXPECT_TRUE(is_confirmed(track_with(0, detection_at(10.0, 10.0), 3)));
}

// Verifies: REQ-TRK-002 — a track detected more than three times stays
// confirmed; the rule is a floor, not an equality.
TEST(Confirmation, HoldsBeyondTheThirdConsecutiveDetection) {
  for (std::uint32_t count = confirmation_frame_count; count < confirmation_frame_count + 5;
       ++count) {
    EXPECT_TRUE(is_confirmed(track_with(0, detection_at(10.0, 10.0), count)))
        << "count " << count << " should remain confirmed";
  }
}

// Verifies: REQ-TRK-008 — with nothing confirmed there is nothing to engage,
// and the absence is explicit rather than an arbitrary track.
TEST(TargetSelection, SelectsNothingWhenNoTrackIsConfirmed) {
  const std::vector<Track> tracks{
      track_with(0, detection_at(100.0, 100.0, 40.0, 40.0), 1),
      track_with(1, detection_at(300.0, 200.0, 80.0, 80.0), 2),
  };

  EXPECT_FALSE(select_confirmed_target(tracks).has_value());
}

// Verifies: REQ-TRK-008 — "the detection with the largest bounding-box area"
// is the one engaged.
TEST(TargetSelection, SelectsTheLargestConfirmedBoundingBox) {
  const std::vector<Track> tracks{
      track_with(0, detection_at(100.0, 100.0, 20.0, 20.0), 3),
      track_with(1, detection_at(300.0, 200.0, 40.0, 30.0), 4),
      track_with(2, detection_at(500.0, 300.0, 10.0, 90.0), 3),
  };

  const std::optional<Track> selected = select_confirmed_target(tracks);

  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(selected->id, tracks[1].id) << "1200 px^2 is the largest of 400, 1200 and 900";
}

// Verifies: REQ-TRK-008 — "a frame containing three confirmable tracks
// produces exactly one engagement".
TEST(TargetSelection, SelectsExactlyOneOfThreeConfirmableTracks) {
  const std::vector<Track> tracks{
      track_with(0, detection_at(100.0, 100.0, 30.0, 30.0), 3),
      track_with(1, detection_at(300.0, 200.0, 30.0, 30.0), 3),
      track_with(2, detection_at(500.0, 300.0, 30.0, 30.0), 3),
  };

  const std::optional<Track> selected = select_confirmed_target(tracks);

  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(
      std::ranges::count_if(tracks, [&selected](const Track& track) { return track == *selected; }),
      1)
      << "the selected track must be one of the candidates, and only one";
}

// Verifies: REQ-TRK-008 — "ties SHALL be broken deterministically by ascending
// X then ascending Y centroid coordinate": equal areas, so the smaller X wins.
TEST(TargetSelection, BreaksAnAreaTieByAscendingX) {
  const std::vector<Track> tracks{
      track_with(0, detection_at(400.0, 100.0, 30.0, 30.0), 3),
      track_with(1, detection_at(120.0, 100.0, 30.0, 30.0), 3),
      track_with(2, detection_at(250.0, 100.0, 30.0, 30.0), 3),
  };

  const std::optional<Track> selected = select_confirmed_target(tracks);

  ASSERT_TRUE(selected.has_value());
  EXPECT_DOUBLE_EQ(selected->latest.centroid_px.x_px, 120.0);
}

// Verifies: REQ-TRK-008 — equal area and equal X: the smaller Y wins.
TEST(TargetSelection, BreaksAnAreaAndXTieByAscendingY) {
  const std::vector<Track> tracks{
      track_with(0, detection_at(200.0, 380.0, 30.0, 30.0), 3),
      track_with(1, detection_at(200.0, 60.0, 30.0, 30.0), 3),
      track_with(2, detection_at(200.0, 210.0, 30.0, 30.0), 3),
  };

  const std::optional<Track> selected = select_confirmed_target(tracks);

  ASSERT_TRUE(selected.has_value());
  EXPECT_DOUBLE_EQ(selected->latest.centroid_px.y_px, 60.0);
}

// Verifies: REQ-TRK-008, REQ-DEV-002 — "two detections of identical area
// produce a stable, documented choice, and the same choice on every run".
TEST(TargetSelection, MakesTheSameChoiceOnEveryRunForIdenticalAreas) {
  constexpr std::uint_fast32_t seed = 20260916;
  const std::vector<Track> tracks{
      track_with(0, detection_at(200.0, 100.0, 30.0, 30.0), 3),
      track_with(1, detection_at(200.0, 100.0, 30.0, 30.0), 5),
      track_with(2, detection_at(340.0, 100.0, 30.0, 30.0), 3),
      track_with(3, detection_at(200.0, 260.0, 30.0, 30.0), 4),
  };

  const std::optional<Track> reference = select_confirmed_target(tracks);
  ASSERT_TRUE(reference.has_value());

  std::mt19937 generator{seed};
  std::vector<Track> permuted = tracks;
  for (int attempt = 0; attempt < 20; ++attempt) {
    std::shuffle(permuted.begin(), permuted.end(), generator);
    const std::optional<Track> selected = select_confirmed_target(permuted);

    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->latest, reference->latest)
        << "attempt " << attempt << " (seed " << seed
        << "): selection depended on the order the tracks happened to be in";
  }
}

// Verifies: REQ-TRK-002, REQ-TRK-008 — "considers only confirmed tracks": an
// unconfirmed bird is never engaged, however large it is. This is the
// `REQ-SAF-002` rule seen from the selection side.
TEST(TargetSelection, IgnoresUnconfirmedTracksHoweverLarge) {
  const std::vector<Track> tracks{
      track_with(0, detection_at(100.0, 100.0, 200.0, 200.0), 2),
      track_with(1, detection_at(300.0, 200.0, 10.0, 10.0), 3),
  };

  const std::optional<Track> selected = select_confirmed_target(tracks);

  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(selected->id, tracks[1].id)
      << "a 40000 px^2 bird seen twice must not outrank a 100 px^2 bird seen three times";
}

// Verifies: REQ-TRK-008 — an empty track set engages nothing.
TEST(TargetSelection, SelectsNothingFromAnEmptyTrackSet) {
  EXPECT_FALSE(select_confirmed_target({}).has_value());
}

}  // namespace
