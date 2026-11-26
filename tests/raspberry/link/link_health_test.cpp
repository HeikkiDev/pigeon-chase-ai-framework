// Verifies: REQ-COM-003, REQ-COM-002, REQ-SAF-008
//
// The health rule of `docs/architecture/device-protocol.md` section 7, as one
// function, and the snapshot of the state it reads, packed into one word
// (ADR-0023). The host link answers `health()` with this rule, and the wrapper
// that runs a reader thread answers it from the snapshot, without a lock; both
// must give the same answer from the same state, so the rule exists once.

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

#include "link_support/simulated_device.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/raspberry/link_health.hpp"
#include "pigeon/raspberry/serial_actuator_link.hpp"
#include "support/manual_clock.hpp"

namespace {

using pigeon::core::LinkStatus;
using pigeon::raspberry::health_from;
using pigeon::raspberry::LinkHealthState;
using pigeon::raspberry::pack;
using pigeon::raspberry::SerialActuatorLink;
using pigeon::raspberry::unpack;
using pigeon::test_support::ManualClock;
using pigeon::test_support::SimulatedDevice;
using std::chrono::milliseconds;

constexpr milliseconds kSilence{1500};

LinkHealthState state(bool open, std::optional<milliseconds> heard, bool faulted, bool rejected) {
  LinkHealthState s;
  s.open = open;
  s.last_heard = heard;
  s.faulted = faulted;
  s.rejected = rejected;
  return s;
}

// --- the rule ---------------------------------------------------------------

TEST(HealthRule, AClosedLineIsUnavailableWhateverElseIsTrue) {
  for (const bool faulted : {false, true}) {
    for (const bool rejected : {false, true}) {
      EXPECT_EQ(
          health_from(state(false, milliseconds{0}, faulted, rejected), milliseconds{0}, kSilence),
          LinkStatus::UNAVAILABLE);
    }
  }
}

TEST(HealthRule, AnOpenLineNeverHeardIsUnavailable) {
  EXPECT_EQ(health_from(state(true, std::nullopt, false, false), milliseconds{0}, kSilence),
            LinkStatus::UNAVAILABLE);
}

TEST(HealthRule, SilenceForTheTimeoutIsUnavailableAndNotBefore) {
  const auto heard = state(true, milliseconds{1000}, false, false);

  EXPECT_EQ(health_from(heard, milliseconds{2499}, kSilence), LinkStatus::OK);
  EXPECT_EQ(health_from(heard, milliseconds{2500}, kSilence), LinkStatus::UNAVAILABLE);
}

TEST(HealthRule, FaultOutranksRejectionAndSilenceOutranksBoth) {
  EXPECT_EQ(health_from(state(true, milliseconds{0}, true, true), milliseconds{0}, kSilence),
            LinkStatus::TRANSPORT_FAILURE);
  EXPECT_EQ(health_from(state(true, milliseconds{0}, false, true), milliseconds{0}, kSilence),
            LinkStatus::REJECTED);
  EXPECT_EQ(health_from(state(true, milliseconds{0}, true, true), milliseconds{1500}, kSilence),
            LinkStatus::UNAVAILABLE);
}

// --- the snapshot -----------------------------------------------------------

TEST(HealthSnapshot, FitsInOneWordThatIsAlwaysLockFree) {
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
  static_assert(noexcept(pack(LinkHealthState{})));
  static_assert(noexcept(unpack(std::uint64_t{0})));
}

TEST(HealthSnapshot, EveryStateSurvivesPackingUnchanged) {
  const std::array<std::optional<milliseconds>, 5> times{std::nullopt, milliseconds{0},
                                                         milliseconds{1}, milliseconds{1500},
                                                         milliseconds{std::int64_t{1} << 58}};
  for (const bool open : {false, true}) {
    for (const auto heard : times) {
      for (const bool faulted : {false, true}) {
        for (const bool rejected : {false, true}) {
          const auto original = state(open, heard, faulted, rejected);
          const auto round_trip = unpack(pack(original));
          EXPECT_EQ(round_trip.open, original.open);
          EXPECT_EQ(round_trip.last_heard, original.last_heard);
          EXPECT_EQ(round_trip.faulted, original.faulted);
          EXPECT_EQ(round_trip.rejected, original.rejected);
        }
      }
    }
  }
}

TEST(HealthSnapshot, ATimeBeforeTheEpochIsKeptAsTheEpochWhichOnlyEverMeansLessHealthy) {
  // It cannot be packed as it is. Moving it earlier can only make the silence
  // look longer, so the snapshot errs towards UNAVAILABLE, never towards OK.
  const auto round_trip = unpack(pack(state(true, milliseconds{-5}, false, false)));

  EXPECT_EQ(round_trip.last_heard, milliseconds{0});
}

// --- the link answers with the same rule --------------------------------------

TEST(HealthRule, TheLinkAnswersWithTheRuleAppliedToItsOwnState) {
  ManualClock clock;
  SimulatedDevice device{clock};
  SerialActuatorLink link{device, clock};
  auto agrees = [&] {
    return link.health() == health_from(link.health_state(), clock.now().since_epoch, kSilence);
  };

  EXPECT_TRUE(agrees());  // never heard
  device.emit_heartbeat();
  link.poll();
  EXPECT_TRUE(agrees());  // heard
  EXPECT_TRUE(link.health_state().last_heard.has_value());

  device.answer_with_result(1);
  ASSERT_EQ(link.send_fire_command({milliseconds{300}}), LinkStatus::REJECTED);
  EXPECT_TRUE(link.health_state().rejected);
  EXPECT_TRUE(agrees());

  device.stay_silent();
  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
  EXPECT_TRUE(link.health_state().faulted);
  EXPECT_TRUE(agrees());

  clock.advance_by(milliseconds{2000});
  EXPECT_TRUE(agrees());  // silent
  device.close();
  EXPECT_FALSE(link.health_state().open);
  EXPECT_TRUE(agrees());  // closed
}

}  // namespace
