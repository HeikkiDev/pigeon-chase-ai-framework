// Verifies: REQ-AIM-002, REQ-COM-002, REQ-SAF-001, REQ-SAF-002, REQ-SAF-003,
//           REQ-SAF-004, REQ-SAF-005, REQ-SAF-006, REQ-SAF-007, REQ-DEV-002
//
// The guard between an intent to fire and a fire command. Every test here is
// about a rule that must be able to say *no*, so the refusal reason is
// asserted as well as the refusal itself: "it did not fire" is not a
// diagnosis.

#include <chrono>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/aiming.hpp"
#include "pigeon/core/configuration.hpp"
#include "pigeon/core/safety_policy.hpp"
#include "pigeon/core/target_state.hpp"
#include "support/detection_builders.hpp"
#include "support/manual_clock.hpp"
#include "support/test_configuration.hpp"

namespace {

using std::chrono::milliseconds;

using pigeon::core::AimingCommand;
using pigeon::core::Angle;
using pigeon::core::Configuration;
using pigeon::core::EngagementIntent;
using pigeon::core::FireAuthorisation;
using pigeon::core::FireRefusal;
using pigeon::core::LinkStatus;
using pigeon::core::SafetyPolicy;
using pigeon::core::ServoAngles;
using pigeon::core::TargetMachineState;
using pigeon::core::TargetTransition;
using pigeon::test_support::calibrated_configuration;
using pigeon::test_support::detection_at;
using pigeon::test_support::exclusion_zone;
using pigeon::test_support::ManualClock;

constexpr ServoAngles straight_ahead{.x = Angle{0.0}, .y = Angle{10.0}};

[[nodiscard]] TargetTransition transition_with(EngagementIntent intent) {
  TargetTransition transition;
  transition.next = TargetMachineState{};
  transition.intent = intent;
  if (intent == EngagementIntent::AIM_AT_TARGET || intent == EngagementIntent::FIRE_AT_TARGET) {
    transition.aim_at = detection_at(320.0, 240.0);
  }
  return transition;
}

[[nodiscard]] TargetTransition fire_intent() {
  return transition_with(EngagementIntent::FIRE_AT_TARGET);
}

// ---------------------------------------------------------------------------
// REQ-SAF-002 / REQ-SAF-004 — refusal is the default.
// ---------------------------------------------------------------------------

// Verifies: REQ-SAF-004 — "refused, NOT_CONFIRMED: the safe default", so a
// forgotten assignment denies a burst rather than granting one.
TEST(FireAuthorisationContract, IsRefusedAndNotConfirmedByDefault) {
  const FireAuthorisation authorisation;

  EXPECT_FALSE(authorisation.granted().has_value());
  EXPECT_EQ(authorisation.refusal_reason(), FireRefusal::NOT_CONFIRMED);
}

// Verifies: REQ-SAF-002 — "the system SHALL NOT issue a fire command unless
// REQ-TRK-002 and REQ-TRK-004 have both been satisfied": the policy refuses
// anything that is not the state machine's fire intent, which is the only
// intent produced after confirmation and re-verification.
TEST(SafetyPolicyFire, RefusesEveryIntentOtherThanFireAtTarget) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  for (const EngagementIntent intent :
       {EngagementIntent::KEEP_SEARCHING, EngagementIntent::AIM_AT_TARGET,
        EngagementIntent::ABANDON}) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(transition_with(intent), straight_ahead, LinkStatus::OK);

    EXPECT_FALSE(authorisation.granted().has_value())
        << "intent " << static_cast<int>(intent) << " authorised a burst";
    EXPECT_EQ(authorisation.refusal_reason(), FireRefusal::NOT_CONFIRMED);
  }
}

// Verifies: REQ-AIM-002, REQ-SAF-004 — "a default-constructed Configuration
// yields a policy that refuses every burst".
TEST(SafetyPolicyFire, RefusesEveryBurstWhenTheRigIsUnconfigured) {
  ManualClock clock;
  SafetyPolicy policy{Configuration{}, clock};

  for (const double y_degrees : {0.0, 10.0, 45.0}) {
    const FireAuthorisation authorisation = policy.authorise_fire(
        fire_intent(), ServoAngles{.x = Angle{0.0}, .y = Angle{y_degrees}}, LinkStatus::OK);

    EXPECT_FALSE(authorisation.granted().has_value());
    EXPECT_EQ(authorisation.refusal_reason(), FireRefusal::NO_ENVELOPE);
  }
}

// Verifies: REQ-SAF-001, REQ-SAF-002 — a confirmed engagement over a working
// link, inside the envelope and outside any zone, is granted, with a burst
// bounded by the configured maximum.
TEST(SafetyPolicyFire, GrantsAConfirmedEngagementWithABoundedBurst) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  const FireAuthorisation authorisation =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);

  ASSERT_TRUE(authorisation.granted().has_value());
  EXPECT_EQ(authorisation.granted()->aim, straight_ahead);
  EXPECT_EQ(authorisation.granted()->duration, milliseconds{500})
      << "the shipped maximum burst is 500 ms";
}

// Verifies: REQ-SAF-001 — "a single fire command SHALL activate the water
// actuator for no longer than a configured maximum duration": the granted
// duration follows the configuration, not the default.
TEST(SafetyPolicyFire, NeverGrantsALongerBurstThanConfigured) {
  ManualClock clock;
  Configuration configuration = calibrated_configuration();
  configuration.safety.max_fire_duration = milliseconds{120};
  SafetyPolicy policy{configuration, clock};

  const FireAuthorisation authorisation =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);

  ASSERT_TRUE(authorisation.granted().has_value());
  EXPECT_LE(authorisation.granted()->duration, configuration.safety.max_fire_duration);
  EXPECT_EQ(authorisation.granted()->duration, milliseconds{120});
}

// Verifies: REQ-AIM-002 — "no angle outside the configured envelope is ever
// emitted": a granted burst carries the clamped aim, including the
// never-below-the-horizon clamp on Y.
TEST(SafetyPolicyFire, GrantsOnlyClampedAngles) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  const FireAuthorisation authorisation = policy.authorise_fire(
      fire_intent(), ServoAngles{.x = Angle{170.0}, .y = Angle{-40.0}}, LinkStatus::OK);

  ASSERT_TRUE(authorisation.granted().has_value());
  EXPECT_EQ(authorisation.granted()->aim.x, Angle{90.0});
  EXPECT_EQ(authorisation.granted()->aim.y, Angle{0.0})
      << "the nozzle is never commanded below the horizon";
}

// ---------------------------------------------------------------------------
// REQ-COM-002 — a link that is not there.
// ---------------------------------------------------------------------------

// Verifies: REQ-COM-002 — "if the actuator link becomes unavailable, the
// system SHALL NOT issue a fire command".
TEST(SafetyPolicyFire, RefusesWhenTheLinkIsUnavailable) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  const FireAuthorisation authorisation =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::UNAVAILABLE);

  EXPECT_FALSE(authorisation.granted().has_value());
  EXPECT_EQ(authorisation.refusal_reason(), FireRefusal::LINK_UNAVAILABLE);
}

// ---------------------------------------------------------------------------
// REQ-SAF-003 / REQ-SAF-006 — the exclusion zone.
// ---------------------------------------------------------------------------

// Verifies: REQ-SAF-003, REQ-SAF-006 — "a target that resolves to angles
// inside a configured exclusion zone produces no fire command", and "angles
// inside both intervals produce no fire command".
TEST(SafetyPolicyFire, RefusesInsideAConfiguredExclusionZone) {
  ManualClock clock;
  Configuration configuration = calibrated_configuration();
  configuration.safety.exclusion_zone = exclusion_zone(-10.0, 10.0, 0.0, 20.0);
  SafetyPolicy policy{configuration, clock};

  for (const ServoAngles aim : {ServoAngles{.x = Angle{0.0}, .y = Angle{10.0}},
                                ServoAngles{.x = Angle{-10.0}, .y = Angle{0.0}},
                                ServoAngles{.x = Angle{10.0}, .y = Angle{20.0}}}) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(fire_intent(), aim, LinkStatus::OK);

    EXPECT_FALSE(authorisation.granted().has_value())
        << "fired at (" << aim.x.degrees << ", " << aim.y.degrees << "), inside the zone";
    EXPECT_EQ(authorisation.refusal_reason(), FireRefusal::EXCLUSION_ZONE);
  }
}

// Verifies: REQ-SAF-006 — "angles inside one interval but not the other permit
// firing": a zone is the conjunction of its two axis ranges, not their union.
TEST(SafetyPolicyFire, PermitsFiringInsideOnlyOneIntervalOfTheZone) {
  ManualClock clock;
  Configuration configuration = calibrated_configuration();
  configuration.safety.exclusion_zone = exclusion_zone(-10.0, 10.0, 0.0, 20.0);
  SafetyPolicy policy{configuration, clock};

  const ServoAngles inside_x_only{.x = Angle{5.0}, .y = Angle{35.0}};
  const ServoAngles inside_y_only{.x = Angle{60.0}, .y = Angle{5.0}};
  const ServoAngles outside_both{.x = Angle{60.0}, .y = Angle{35.0}};

  for (const ServoAngles aim : {inside_x_only, inside_y_only, outside_both}) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(fire_intent(), aim, LinkStatus::OK);

    EXPECT_TRUE(authorisation.granted().has_value())
        << "refused at (" << aim.x.degrees << ", " << aim.y.degrees
        << "), which is outside the zone rectangle";
  }
}

// Verifies: REQ-SAF-003, REQ-SAF-006 — "with no exclusion zone configured,
// firing is permitted anywhere within the mechanical envelope".
TEST(SafetyPolicyFire, PermitsFiringAnywhereInTheEnvelopeWithNoZoneConfigured) {
  ManualClock clock;
  const Configuration configuration = calibrated_configuration();
  ASSERT_FALSE(configuration.safety.exclusion_zone.has_value());
  SafetyPolicy policy{configuration, clock};

  for (int x_degrees = -90; x_degrees <= 90; x_degrees += 15) {
    for (int y_degrees = 0; y_degrees <= 45; y_degrees += 5) {
      const ServoAngles aim{.x = Angle{static_cast<double>(x_degrees)},
                            .y = Angle{static_cast<double>(y_degrees)}};
      const FireAuthorisation authorisation =
          policy.authorise_fire(fire_intent(), aim, LinkStatus::OK);

      ASSERT_TRUE(authorisation.granted().has_value())
          << "refused at (" << x_degrees << ", " << y_degrees << ") with no zone configured";
      EXPECT_EQ(authorisation.granted()->aim, aim);
    }
  }
}

// ---------------------------------------------------------------------------
// REQ-SAF-005 / REQ-SAF-007 — cool-down and engagement rate, on an injected
// clock.
// ---------------------------------------------------------------------------

// Verifies: REQ-SAF-005 — "a fire command immediately followed by another
// confirmed target produces no second fire command until the cool-down has
// elapsed", and "rate limiting is driven by an injected monotonic clock".
TEST(SafetyPolicyRateLimit, RefusesASecondBurstUntilTheCoolDownHasElapsed) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  ASSERT_TRUE(policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK)
                  .granted()
                  .has_value());
  policy.record_fire_sent();

  const FireAuthorisation immediately =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);
  EXPECT_FALSE(immediately.granted().has_value());
  EXPECT_EQ(immediately.refusal_reason(), FireRefusal::COOLING_DOWN);

  clock.advance_by(milliseconds{1999});
  const FireAuthorisation just_before =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);
  EXPECT_FALSE(just_before.granted().has_value())
      << "1999 ms is inside the 2 s cool-down";
  EXPECT_EQ(just_before.refusal_reason(), FireRefusal::COOLING_DOWN);

  clock.advance_by(milliseconds{2});
  const FireAuthorisation after =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);
  EXPECT_TRUE(after.granted().has_value()) << "2001 ms is past the 2 s cool-down";
}

// Verifies: REQ-SAF-005 — "a sequence of confirmed targets arriving faster
// than the configured rate produces no more than the configured number of fire
// commands per minute", and the window is a sliding one on the injected clock.
TEST(SafetyPolicyRateLimit, AllowsNoMoreThanTheConfiguredEngagementsPerMinute) {
  ManualClock clock;
  const Configuration configuration = calibrated_configuration();
  SafetyPolicy policy{configuration, clock};

  // Six bursts, each well past the cool-down, all inside one minute.
  for (std::uint32_t burst = 0; burst < configuration.safety.max_engagements_per_minute; ++burst) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);
    ASSERT_TRUE(authorisation.granted().has_value()) << "burst " << burst << " was refused";
    policy.record_fire_sent();
    clock.advance_by(milliseconds{3000});
  }

  const FireAuthorisation seventh =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);
  EXPECT_FALSE(seventh.granted().has_value())
      << "a seventh burst inside the same minute must be refused";
  EXPECT_EQ(seventh.refusal_reason(), FireRefusal::RATE_LIMIT_REACHED);

  // Far enough on that the earliest burst has left the one-minute window.
  clock.advance_by(milliseconds{45001});
  const FireAuthorisation later =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);
  EXPECT_TRUE(later.granted().has_value())
      << "the rate window slides: bursts older than a minute no longer count";
}

// Verifies: REQ-SAF-007 — "authorising is not firing": `authorise_fire` records
// nothing, so a burst that was never transmitted starts no cool-down. Two
// consecutive authorisations with nothing sent in between are both granted.
TEST(SafetyPolicyRateLimit, AuthorisingDoesNotItselfStartTheCoolDown) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  const FireAuthorisation first =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);
  const FireAuthorisation second =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);

  EXPECT_TRUE(first.granted().has_value());
  EXPECT_TRUE(second.granted().has_value())
      << "a permission that was never acted on must not consume an engagement";
}

// Verifies: REQ-SAF-007 — "a simulated link that rejects a fire command still
// starts the cool-down": the command left the Raspberry Pi, so it counts,
// whatever came back (ADR-0011).
TEST(SafetyPolicyRateLimit, ATransmittedButRejectedBurstStillStartsTheCoolDown) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  ASSERT_TRUE(policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK)
                  .granted()
                  .has_value());
  // The caller transmitted the command and the device rejected it; the caller
  // records it regardless.
  policy.record_fire_sent();

  const FireAuthorisation next =
      policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);
  EXPECT_FALSE(next.granted().has_value());
  EXPECT_EQ(next.refusal_reason(), FireRefusal::COOLING_DOWN);
}

// Verifies: REQ-SAF-007 — "a burst refused before transmission — empty
// envelope, exclusion zone, rate limit — starts no cool-down and consumes no
// engagement".
TEST(SafetyPolicyRateLimit, ABurstRefusedBeforeTransmissionStartsNoCoolDown) {
  ManualClock clock;
  Configuration configuration = calibrated_configuration();
  configuration.safety.exclusion_zone = exclusion_zone(-10.0, 10.0, 0.0, 20.0);
  SafetyPolicy policy{configuration, clock};

  const ServoAngles inside_zone{.x = Angle{0.0}, .y = Angle{10.0}};
  const FireAuthorisation refused =
      policy.authorise_fire(fire_intent(), inside_zone, LinkStatus::OK);
  ASSERT_FALSE(refused.granted().has_value());
  ASSERT_EQ(refused.refusal_reason(), FireRefusal::EXCLUSION_ZONE);

  const ServoAngles outside_zone{.x = Angle{60.0}, .y = Angle{35.0}};
  const FireAuthorisation next =
      policy.authorise_fire(fire_intent(), outside_zone, LinkStatus::OK);
  EXPECT_TRUE(next.granted().has_value())
      << "a burst that never left the Raspberry Pi must not consume an engagement";
}

// Verifies: REQ-DEV-002 — the policy reads time only through the injected
// clock: with the clock held still, the cool-down never elapses however many
// times it is asked.
TEST(SafetyPolicyRateLimit, ReadsTimeOnlyThroughTheInjectedClock) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  ASSERT_TRUE(policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK)
                  .granted()
                  .has_value());
  policy.record_fire_sent();

  for (int attempt = 0; attempt < 50; ++attempt) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(fire_intent(), straight_ahead, LinkStatus::OK);
    ASSERT_FALSE(authorisation.granted().has_value())
        << "attempt " << attempt << ": time passed without the clock moving";
    ASSERT_EQ(authorisation.refusal_reason(), FireRefusal::COOLING_DOWN);
  }
}

// ---------------------------------------------------------------------------
// REQ-AIM-002 — aiming authorisation. Pointing is not firing.
// ---------------------------------------------------------------------------

// Verifies: REQ-AIM-002 — an aiming command is clamped to the envelope before
// it reaches the actuator system.
TEST(SafetyPolicyAim, ClampsAnAimingCommandToTheEnvelope) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  const std::optional<AimingCommand> command =
      policy.authorise_aim(ServoAngles{.x = Angle{130.0}, .y = Angle{-15.0}});

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(command->angles.x, Angle{90.0});
  EXPECT_EQ(command->angles.y, Angle{0.0});
}

// Verifies: REQ-AIM-002, REQ-SAF-004 — "returns nullopt when the envelope is
// empty, so an unconfigured rig is never commanded to move".
TEST(SafetyPolicyAim, CommandsNothingWhenTheEnvelopeIsEmpty) {
  ManualClock clock;
  SafetyPolicy policy{Configuration{}, clock};

  EXPECT_FALSE(policy.authorise_aim(ServoAngles{}).has_value());
  EXPECT_FALSE(policy.authorise_aim(ServoAngles{.x = Angle{10.0}, .y = Angle{10.0}}).has_value());
}

// Verifies: REQ-SAF-003, REQ-SAF-005 — "aiming is not gated by the exclusion
// zone or the rate limit: pointing is not firing" (safety_policy.hpp). The rig
// may track a bird it is forbidden to spray.
TEST(SafetyPolicyAim, IsNotGatedByTheExclusionZoneOrTheCoolDown) {
  ManualClock clock;
  Configuration configuration = calibrated_configuration();
  configuration.safety.exclusion_zone = exclusion_zone(-10.0, 10.0, 0.0, 20.0);
  SafetyPolicy policy{configuration, clock};

  const ServoAngles inside_zone{.x = Angle{0.0}, .y = Angle{10.0}};
  EXPECT_TRUE(policy.authorise_aim(inside_zone).has_value());

  ASSERT_TRUE(policy.authorise_fire(fire_intent(), ServoAngles{.x = Angle{60.0}, .y = Angle{35.0}},
                                    LinkStatus::OK)
                  .granted()
                  .has_value());
  policy.record_fire_sent();

  EXPECT_TRUE(policy.authorise_aim(inside_zone).has_value())
      << "a cooling-down rig may still point at a bird";
}

}  // namespace
