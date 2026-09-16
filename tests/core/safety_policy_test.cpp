// Verifies: REQ-AIM-002, REQ-COM-002, REQ-COM-003, REQ-SAF-001, REQ-SAF-002,
//           REQ-SAF-003, REQ-SAF-004, REQ-SAF-005, REQ-SAF-006, REQ-SAF-007,
//           REQ-SAF-008, REQ-DEV-002
//
// The guard between an intent to fire and a fire command. Every test here is
// about a rule that must be able to say *no*, so the refusal reason is
// asserted as well as the refusal itself: "it did not fire" is not a
// diagnosis.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/aiming.hpp"
#include "pigeon/core/configuration.hpp"
#include "pigeon/core/safety_policy.hpp"
#include "pigeon/core/target_state.hpp"
#include "support/detection_builders.hpp"
#include "support/manual_clock.hpp"
#include "support/simulated_actuator_link.hpp"
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
using pigeon::core::refusal_for;
using pigeon::core::SafetyPolicy;
using pigeon::core::ServoAngles;
using pigeon::core::TargetMachineState;
using pigeon::core::TargetTransition;
using pigeon::test_support::calibrated_configuration;
using pigeon::test_support::detection_at;
using pigeon::test_support::exclusion_zone;
using pigeon::test_support::ManualClock;
using pigeon::test_support::SimulatedActuatorLink;

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
  SimulatedActuatorLink healthy_link{clock};
  SafetyPolicy policy{calibrated_configuration(), clock};

  for (const EngagementIntent intent :
       {EngagementIntent::KEEP_SEARCHING, EngagementIntent::AIM_AT_TARGET,
        EngagementIntent::ABANDON}) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(transition_with(intent), straight_ahead, healthy_link);

    EXPECT_FALSE(authorisation.granted().has_value())
        << "intent " << static_cast<int>(intent) << " authorised a burst";
    EXPECT_EQ(authorisation.refusal_reason(), FireRefusal::NOT_CONFIRMED);
  }
}

// Verifies: REQ-AIM-002, REQ-SAF-004 — "a default-constructed Configuration
// yields a policy that refuses every burst".
TEST(SafetyPolicyFire, RefusesEveryBurstWhenTheRigIsUnconfigured) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  SafetyPolicy policy{Configuration{}, clock};

  for (const double y_degrees : {0.0, 10.0, 45.0}) {
    const FireAuthorisation authorisation = policy.authorise_fire(
        fire_intent(), ServoAngles{.x = Angle{0.0}, .y = Angle{y_degrees}}, healthy_link);

    EXPECT_FALSE(authorisation.granted().has_value());
    EXPECT_EQ(authorisation.refusal_reason(), FireRefusal::NO_ENVELOPE);
  }
}

// Verifies: REQ-SAF-001, REQ-SAF-002 — a confirmed engagement over a working
// link, inside the envelope and outside any zone, is granted, with a burst
// bounded by the configured maximum.
TEST(SafetyPolicyFire, GrantsAConfirmedEngagementWithABoundedBurst) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  SafetyPolicy policy{calibrated_configuration(), clock};

  const FireAuthorisation authorisation =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);

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
  SimulatedActuatorLink healthy_link{clock};
  Configuration configuration = calibrated_configuration();
  configuration.safety.max_fire_duration = milliseconds{120};
  SafetyPolicy policy{configuration, clock};

  const FireAuthorisation authorisation =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);

  ASSERT_TRUE(authorisation.granted().has_value());
  EXPECT_LE(authorisation.granted()->duration, configuration.safety.max_fire_duration);
  EXPECT_EQ(authorisation.granted()->duration, milliseconds{120});
}

// Verifies: REQ-AIM-002 — "no angle outside the configured envelope is ever
// emitted": a granted burst carries the clamped aim, including the
// never-below-the-horizon clamp on Y.
TEST(SafetyPolicyFire, GrantsOnlyClampedAngles) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  SafetyPolicy policy{calibrated_configuration(), clock};

  const FireAuthorisation authorisation = policy.authorise_fire(
      fire_intent(), ServoAngles{.x = Angle{170.0}, .y = Angle{-40.0}}, healthy_link);

  ASSERT_TRUE(authorisation.granted().has_value());
  EXPECT_EQ(authorisation.granted()->aim.x, Angle{90.0});
  EXPECT_EQ(authorisation.granted()->aim.y, Angle{0.0})
      << "the nozzle is never commanded below the horizon";
}

// ---------------------------------------------------------------------------
// REQ-COM-002 — a link that is not there.
// ---------------------------------------------------------------------------

// Verifies: REQ-COM-002, REQ-COM-003 — "if the actuator link becomes
// unavailable, the system SHALL NOT issue a fire command", and "a link that
// has become unavailable is refused on the strength of its reported health
// alone, with no command sent to discover the fault".
TEST(SafetyPolicyFire, RefusesWhenTheLinkIsUnavailable) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  link.set_status(LinkStatus::UNAVAILABLE);
  SafetyPolicy policy{calibrated_configuration(), clock};

  const FireAuthorisation authorisation =
      policy.authorise_fire(fire_intent(), straight_ahead, link);

  EXPECT_FALSE(authorisation.granted().has_value());
  EXPECT_EQ(authorisation.refusal_reason(), FireRefusal::LINK_UNAVAILABLE);
  EXPECT_EQ(link.transmissions(), 0)
      << "the fault was learned by asking, not by sending something and watching it fail";
}

// ---------------------------------------------------------------------------
// REQ-SAF-008 — only a healthy link may fire.
// ---------------------------------------------------------------------------

// Every value of `LinkStatus`, in declaration order.
//
// Hand-written because the enumeration offers no count and ADR-0015 rejected a
// `COUNT` sentinel on purpose. The assertion below pins the shape the list was
// written against, in the same terms the header uses, so a status that is
// *inserted, reordered or removed* stops this file compiling with a message
// saying what to do.
//
// It does **not** catch a status *appended* after `REJECTED`: appending leaves
// every existing value unchanged, so neither this assertion nor the one in
// `safety_policy.hpp` can see it. That case is caught at compile time by
// `-Wswitch` on the `default:`-free switch in `refusal_for`'s definition
// (ADR-0015), which is a property of a source file this suite cannot read.
// Runtime exhaustiveness over an open enumeration is not testable without a
// sentinel, and no test here pretends otherwise.
constexpr std::array<LinkStatus, 4> all_link_statuses{
    LinkStatus::OK,
    LinkStatus::UNAVAILABLE,
    LinkStatus::TRANSPORT_FAILURE,
    LinkStatus::REJECTED,
};

static_assert(static_cast<std::uint8_t>(LinkStatus::OK) == 0U &&
                  static_cast<std::uint8_t>(LinkStatus::UNAVAILABLE) == 1U &&
                  static_cast<std::uint8_t>(LinkStatus::TRANSPORT_FAILURE) == 2U &&
                  static_cast<std::uint8_t>(LinkStatus::REJECTED) == 3U,
              "LinkStatus has changed shape. Add the new status to all_link_statuses so that "
              "every test below covers it, and check it has its own FireRefusal "
              "(REQ-SAF-008, ADR-0015).");

// Verifies: REQ-SAF-008 — "every LinkStatus value is either OK or has a
// refusal reason: no status is unhandled", and "only OK yields nullopt".
//
// What this adds over the compile-time mechanisms: `-Wswitch` proves only that
// every enumerator has a *case*, and the header's `static_assert` proves only
// that the enumeration has not changed shape. Neither says anything about the
// values returned. A `refusal_for` that answered `LINK_UNAVAILABLE` for all
// three faults would satisfy both and still be wrong, and that is the mistake
// this test and the next one exist to catch.
TEST(LinkStatusRefusalMapping, RefusesEveryStatusExceptOk) {
  EXPECT_FALSE(refusal_for(LinkStatus::OK).has_value())
      << "OK is the one status that may fire";

  int refusing_statuses = 0;
  for (const LinkStatus status : all_link_statuses) {
    if (status == LinkStatus::OK) {
      continue;
    }
    EXPECT_TRUE(refusal_for(status).has_value())
        << "status " << static_cast<int>(status) << " has no refusal reason";
    ++refusing_statuses;
  }

  // Anti-vacuity: an empty or OK-only list would pass every loop above.
  EXPECT_EQ(refusing_statuses, 3)
      << "the list of statuses under test must cover every non-OK status";
}

// Verifies: REQ-SAF-008 — "the refusal reason differs for each of those three
// statuses, and no two statuses share a reason".
//
// The comparison is derived from `all_link_statuses` rather than written out as
// three named pairs, so a status added to that list is compared against every
// other one without anybody remembering to add a case here.
TEST(LinkStatusRefusalMapping, GivesNoTwoStatusesTheSameReason) {
  std::vector<FireRefusal> reasons;
  for (const LinkStatus status : all_link_statuses) {
    const std::optional<FireRefusal> reason = refusal_for(status);
    if (reason.has_value()) {
      reasons.push_back(*reason);
    }
  }

  ASSERT_EQ(reasons.size(), 3U) << "nothing is proved by comparing fewer than every refusal";
  for (std::size_t first = 0; first < reasons.size(); ++first) {
    for (std::size_t second = first + 1; second < reasons.size(); ++second) {
      EXPECT_NE(reasons[first], reasons[second])
          << "two link statuses share refusal reason " << static_cast<int>(reasons[first])
          << ", so a refusal in a log cannot say which fault occurred";
    }
  }
}

// Verifies: REQ-SAF-008 — "a link reporting OK and an otherwise permitted
// engagement produces a fire command"; "a link reporting an unavailable link, a
// transport failure, or a rejection each produces no fire command"; and the
// reachability bullet, "for each one there is a simulated link reporting it,
// before any command is sent, that produces that status's refusal".
// Verifies: REQ-COM-003 — "each link status is obtainable from a simulated link
// before any command has been sent, and each reaches the fire decision".
//
// The link is built fresh for each status and nothing is ever sent over it, so
// the status under test is the link's *health*, reported on demand, and not the
// residue of an exchange. `transmissions()` is asserted to be zero at the
// moment of the decision, which is what makes that claim checkable rather than
// merely intended. Before `health()` existed, two of these four statuses could
// not be produced at this point at all (ADR-0016).
//
// The expected refusal is read from `refusal_for` rather than written out, so
// the test asserts the policy *uses the one mapping* instead of asserting a
// second, independently maintained copy of it. A policy that hard-coded
// `LINK_UNAVAILABLE` for every fault would fail here.
TEST(SafetyPolicyFire, GrantsOnlyOverALinkReportingOk) {
  ManualClock clock;
  SafetyPolicy policy{calibrated_configuration(), clock};

  int granted = 0;
  int refused = 0;
  for (const LinkStatus status : all_link_statuses) {
    SimulatedActuatorLink link{clock};
    link.set_status(status);
    ASSERT_EQ(link.health(), status)
        << "status " << static_cast<int>(status) << " is not even reportable before a send";
    ASSERT_EQ(link.transmissions(), 0)
        << "the status must be the link's health, not the outcome of an exchange";

    const FireAuthorisation authorisation =
        policy.authorise_fire(fire_intent(), straight_ahead, link);
    const std::optional<FireRefusal> expected = refusal_for(status);

    EXPECT_EQ(link.transmissions(), 0)
        << "deciding whether to fire must send nothing over the link";

    if (expected.has_value()) {
      EXPECT_FALSE(authorisation.granted().has_value())
          << "status " << static_cast<int>(status) << " authorised a burst";
      EXPECT_EQ(authorisation.refusal_reason(), *expected)
          << "status " << static_cast<int>(status)
          << " refused for a reason other than the one refusal_for gives it";
      ++refused;
    } else {
      EXPECT_TRUE(authorisation.granted().has_value())
          << "a healthy link and a confirmed engagement must fire";
      ++granted;
    }
  }

  // Anti-vacuity: the loop must have exercised both answers, not just one.
  EXPECT_EQ(granted, 1) << "exactly one status may fire";
  EXPECT_EQ(refused, 3) << "every other status must refuse";
}

// Verifies: REQ-SAF-008 — "each status SHALL produce its own refusal reason,
// distinct [...] from every other cause of refusal".
//
// The reasons are collected by driving the policy into each cause it can
// refuse for, rather than by listing enumerators, so a collision introduced
// anywhere — a link fault reusing `COOLING_DOWN`, a rate limit reusing
// `LINK_REJECTED` — is caught by the same assertion.
TEST(SafetyPolicyFire, GivesEveryCauseOfRefusalItsOwnReason) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  std::vector<FireRefusal> reasons;

  const auto record = [&reasons](const FireAuthorisation& authorisation, const char* cause) {
    EXPECT_FALSE(authorisation.granted().has_value()) << cause << " did not refuse";
    reasons.push_back(authorisation.refusal_reason());
  };

  // An unconfirmed engagement.
  SafetyPolicy confirmed_policy{calibrated_configuration(), clock};
  record(confirmed_policy.authorise_fire(transition_with(EngagementIntent::KEEP_SEARCHING),
                                         straight_ahead, healthy_link),
         "an unconfirmed engagement");

  // An unconfigured rig: no envelope.
  SafetyPolicy unconfigured_policy{Configuration{}, clock};
  record(unconfigured_policy.authorise_fire(fire_intent(), straight_ahead, healthy_link),
         "an empty envelope");

  // Every link fault. A fresh link per status, reporting it before anything is
  // sent (`REQ-COM-003`).
  for (const LinkStatus status : all_link_statuses) {
    if (status == LinkStatus::OK) {
      continue;
    }
    SimulatedActuatorLink faulty_link{clock};
    faulty_link.set_status(status);
    record(confirmed_policy.authorise_fire(fire_intent(), straight_ahead, faulty_link),
           "a link fault");
  }

  // An aim inside the exclusion zone.
  Configuration zoned = calibrated_configuration();
  zoned.safety.exclusion_zone = exclusion_zone(-10.0, 10.0, 0.0, 20.0);
  SafetyPolicy zoned_policy{zoned, clock};
  record(zoned_policy.authorise_fire(fire_intent(), ServoAngles{.x = Angle{0.0}, .y = Angle{10.0}},
                                     healthy_link),
         "an aim inside the exclusion zone");

  // A burst inside the cool-down.
  SafetyPolicy cooling_policy{calibrated_configuration(), clock};
  ASSERT_TRUE(cooling_policy.authorise_fire(fire_intent(), straight_ahead, healthy_link)
                  .granted()
                  .has_value());
  cooling_policy.record_fire_sent();
  record(cooling_policy.authorise_fire(fire_intent(), straight_ahead, healthy_link),
         "a burst inside the cool-down");

  // A burst past the engagement rate.
  const Configuration configuration = calibrated_configuration();
  SafetyPolicy rate_limited_policy{configuration, clock};
  for (std::uint32_t burst = 0; burst < configuration.safety.max_engagements_per_minute; ++burst) {
    ASSERT_TRUE(rate_limited_policy.authorise_fire(fire_intent(), straight_ahead, healthy_link)
                    .granted()
                    .has_value())
        << "burst " << burst << " was refused while filling the rate window";
    rate_limited_policy.record_fire_sent();
    clock.advance_by(milliseconds{3000});
  }
  record(rate_limited_policy.authorise_fire(fire_intent(), straight_ahead, healthy_link),
         "a burst past the engagement rate");

  // Anti-vacuity: every cause the policy can refuse for must be represented,
  // or the distinctness below is a statement about a subset.
  ASSERT_EQ(reasons.size(), 8U)
      << "one reason per cause: not confirmed, no envelope, three link faults, "
         "exclusion zone, cool-down, rate limit";

  for (std::size_t first = 0; first < reasons.size(); ++first) {
    for (std::size_t second = first + 1; second < reasons.size(); ++second) {
      EXPECT_NE(reasons[first], reasons[second])
          << "causes " << first << " and " << second << " share refusal reason "
          << static_cast<int>(reasons[first]) << ": a refusal must name its cause";
    }
  }
}

// Verifies: REQ-SAF-008 — "then, in order: link health (REQ-SAF-008,
// REQ-COM-002), envelope clamp, the exclusion zone, cool-down and engagement
// rate" (safety_policy.hpp).
//
// A rig that is both unconfigured and talking to a broken link has two reasons
// to refuse. The header fixes which one is reported, so the diagnosis a field
// operator sees is deterministic rather than a function of the order somebody
// happened to write the checks in. This asserts the documented order; it is an
// interface contract rather than an acceptance bullet of its own.
TEST(SafetyPolicyFire, ReportsTheLinkFaultBeforeAnyOtherRefusal) {
  ManualClock clock;
  SafetyPolicy unconfigured_policy{Configuration{}, clock};

  for (const LinkStatus status : all_link_statuses) {
    if (status == LinkStatus::OK) {
      continue;
    }
    SimulatedActuatorLink faulty_link{clock};
    faulty_link.set_status(status);
    const FireAuthorisation authorisation =
        unconfigured_policy.authorise_fire(fire_intent(), straight_ahead, faulty_link);

    EXPECT_FALSE(authorisation.granted().has_value());
    EXPECT_EQ(authorisation.refusal_reason(), *refusal_for(status))
        << "link health is checked before the envelope, so the link fault is the reason reported";
  }
}

// ---------------------------------------------------------------------------
// REQ-COM-003 — current link health is queryable without an exchange.
// ---------------------------------------------------------------------------

// Verifies: REQ-COM-003 — "a fire decision cannot be taken without consulting
// the link: the decision is given the link itself, and no caller-supplied
// status can stand in for it."
//
// This bullet is a statement about the *shape of the interface*, so a
// `static_assert` is its honest encoding and a runtime check could not add to
// it: there is no value one could pass to prove that a different parameter type
// is absent. The first assertion pins the signature; the second states the
// bullet directly — a `LinkStatus` cannot be handed to this function, so the
// caller cannot supply a health nobody observed. Both would fail if an overload
// taking a bare status were ever reintroduced alongside this one, which is the
// regression `REQ-COM-003` exists to prevent (ADR-0016).
static_assert(
    std::is_same_v<decltype(&SafetyPolicy::authorise_fire),
                   FireAuthorisation (SafetyPolicy::*)(const TargetTransition&, ServoAngles,
                                                       const pigeon::core::ActuatorLink&) const>,
    "authorise_fire must take the ActuatorLink and ask it, not accept a status from its caller "
    "(REQ-COM-003, REQ-SAF-008, ADR-0016).");

static_assert(!std::is_invocable_v<decltype(&SafetyPolicy::authorise_fire), const SafetyPolicy&,
                                   const TargetTransition&, ServoAngles, LinkStatus>,
              "a remembered LinkStatus must not be acceptable in place of the link itself "
              "(REQ-COM-003).");

// Verifies: REQ-COM-003 — "querying health transmits nothing: a simulated link
// that counts transmissions records none after any number of queries", and
// "two queries with no exchange between them return the same status, and
// neither changes the actuator system's state."
//
// The obligation is on every implementation of `ActuatorLink`, firmware
// included, so it is asserted on the double that stands in for one. An
// implementation that probed the device to answer — the circularity
// `REQ-COM-003` exists to forbid — would raise the count.
TEST(LinkHealthQuery, TransmitsNothingHoweverOftenItIsAsked) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  link.set_status(LinkStatus::TRANSPORT_FAILURE);

  int queries = 0;
  for (int attempt = 0; attempt < 32; ++attempt) {
    EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE)
        << "two queries with no exchange between them must agree";
    ++queries;
    EXPECT_EQ(link.transmissions(), 0) << "a health query put something on the wire";
  }

  // Anti-vacuity: a loop that never ran would satisfy every assertion above.
  ASSERT_EQ(queries, 32);
  EXPECT_TRUE(link.aiming_commands().empty());
  EXPECT_TRUE(link.fire_commands().empty());
  EXPECT_EQ(link.safe_state_commands(), 0);
  EXPECT_FALSE(link.water_active()) << "asking a question must not change the actuator's state";
}

// Verifies: REQ-COM-003 — "the health used for a fire decision SHALL be
// obtained from this query at the moment of the decision, and SHALL NOT be a
// remembered outcome of an earlier exchange".
//
// The same policy is asked the same question about the same link twice, with
// nothing changed but the link's health in between. The answers must differ. A
// policy that read health once — at construction, or on its first decision, or
// from anything it cached — would give the same answer twice and fail here.
// This is what the `static_assert` above cannot reach: the signature forces the
// link to be *available* at the decision, not that it is *consulted* then.
TEST(SafetyPolicyFire, ReadsLinkHealthAtTheMomentOfEachDecision) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  SafetyPolicy policy{calibrated_configuration(), clock};

  const FireAuthorisation while_healthy =
      policy.authorise_fire(fire_intent(), straight_ahead, link);
  ASSERT_TRUE(while_healthy.granted().has_value())
      << "a healthy link and a confirmed engagement must fire";

  // Nothing is transmitted and nothing is recorded, so the only thing that has
  // changed between the two decisions is what the link reports about itself.
  link.set_status(LinkStatus::REJECTED);
  ASSERT_EQ(link.transmissions(), 0);

  const FireAuthorisation once_degraded =
      policy.authorise_fire(fire_intent(), straight_ahead, link);
  EXPECT_FALSE(once_degraded.granted().has_value())
      << "the policy answered from a health it had remembered, not from the link";
  EXPECT_EQ(once_degraded.refusal_reason(), *refusal_for(LinkStatus::REJECTED));
  EXPECT_EQ(link.transmissions(), 0)
      << "the degraded health was learned by asking, with no command sent to discover it";
}

// ---------------------------------------------------------------------------
// REQ-SAF-003 / REQ-SAF-006 — the exclusion zone.
// ---------------------------------------------------------------------------

// Verifies: REQ-SAF-003, REQ-SAF-006 — "a target that resolves to angles
// inside a configured exclusion zone produces no fire command", and "angles
// inside both intervals produce no fire command".
TEST(SafetyPolicyFire, RefusesInsideAConfiguredExclusionZone) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  Configuration configuration = calibrated_configuration();
  configuration.safety.exclusion_zone = exclusion_zone(-10.0, 10.0, 0.0, 20.0);
  SafetyPolicy policy{configuration, clock};

  for (const ServoAngles aim : {ServoAngles{.x = Angle{0.0}, .y = Angle{10.0}},
                                ServoAngles{.x = Angle{-10.0}, .y = Angle{0.0}},
                                ServoAngles{.x = Angle{10.0}, .y = Angle{20.0}}}) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(fire_intent(), aim, healthy_link);

    EXPECT_FALSE(authorisation.granted().has_value())
        << "fired at (" << aim.x.degrees << ", " << aim.y.degrees << "), inside the zone";
    EXPECT_EQ(authorisation.refusal_reason(), FireRefusal::EXCLUSION_ZONE);
  }
}

// Verifies: REQ-SAF-006 — "angles inside one interval but not the other permit
// firing": a zone is the conjunction of its two axis ranges, not their union.
TEST(SafetyPolicyFire, PermitsFiringInsideOnlyOneIntervalOfTheZone) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  Configuration configuration = calibrated_configuration();
  configuration.safety.exclusion_zone = exclusion_zone(-10.0, 10.0, 0.0, 20.0);
  SafetyPolicy policy{configuration, clock};

  const ServoAngles inside_x_only{.x = Angle{5.0}, .y = Angle{35.0}};
  const ServoAngles inside_y_only{.x = Angle{60.0}, .y = Angle{5.0}};
  const ServoAngles outside_both{.x = Angle{60.0}, .y = Angle{35.0}};

  for (const ServoAngles aim : {inside_x_only, inside_y_only, outside_both}) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(fire_intent(), aim, healthy_link);

    EXPECT_TRUE(authorisation.granted().has_value())
        << "refused at (" << aim.x.degrees << ", " << aim.y.degrees
        << "), which is outside the zone rectangle";
  }
}

// Verifies: REQ-SAF-003, REQ-SAF-006 — "with no exclusion zone configured,
// firing is permitted anywhere within the mechanical envelope".
TEST(SafetyPolicyFire, PermitsFiringAnywhereInTheEnvelopeWithNoZoneConfigured) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  const Configuration configuration = calibrated_configuration();
  ASSERT_FALSE(configuration.safety.exclusion_zone.has_value());
  SafetyPolicy policy{configuration, clock};

  for (int x_degrees = -90; x_degrees <= 90; x_degrees += 15) {
    for (int y_degrees = 0; y_degrees <= 45; y_degrees += 5) {
      const ServoAngles aim{.x = Angle{static_cast<double>(x_degrees)},
                            .y = Angle{static_cast<double>(y_degrees)}};
      const FireAuthorisation authorisation =
          policy.authorise_fire(fire_intent(), aim, healthy_link);

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
  SimulatedActuatorLink healthy_link{clock};
  SafetyPolicy policy{calibrated_configuration(), clock};

  ASSERT_TRUE(policy.authorise_fire(fire_intent(), straight_ahead, healthy_link)
                  .granted()
                  .has_value());
  policy.record_fire_sent();

  const FireAuthorisation immediately =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_FALSE(immediately.granted().has_value());
  EXPECT_EQ(immediately.refusal_reason(), FireRefusal::COOLING_DOWN);

  clock.advance_by(milliseconds{1999});
  const FireAuthorisation just_before =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_FALSE(just_before.granted().has_value())
      << "1999 ms is inside the 2 s cool-down";
  EXPECT_EQ(just_before.refusal_reason(), FireRefusal::COOLING_DOWN);

  clock.advance_by(milliseconds{2});
  const FireAuthorisation after =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_TRUE(after.granted().has_value()) << "2001 ms is past the 2 s cool-down";
}

// Verifies: REQ-SAF-005 — "a burst authorised at **exactly** the cool-down
// duration after the previous transmitted burst is permitted; one a
// millisecond earlier is refused".
//
// The convention is `elapsed >= duration`, so the cool-down that begins at *t*
// occupies the half-open interval [t, t + 2000 ms) and *t* + 2000 ms is the
// first permitted instant (ADR-0014, Q18). This test lands on that instant
// rather than either side of it: a `>` written where `>=` was meant changes the
// answer at exactly one value of the clock, and only a test that evaluates that
// value can see it.
//
// The clock is stepped so that it reads exactly the boundary, and that reading
// is asserted before the question is asked, so the test cannot pass while
// exercising some other instant.
TEST(SafetyPolicyRateLimit, PermitsABurstAtExactlyTheCoolDownDuration) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  const Configuration configuration = calibrated_configuration();
  const milliseconds cool_down = configuration.safety.cool_down;
  SafetyPolicy policy{configuration, clock};

  const milliseconds fired_at = clock.now().since_epoch;
  ASSERT_TRUE(policy.authorise_fire(fire_intent(), straight_ahead, healthy_link)
                  .granted()
                  .has_value());
  policy.record_fire_sent();

  clock.advance_by(cool_down - milliseconds{1});
  ASSERT_EQ(clock.now().since_epoch - fired_at, cool_down - milliseconds{1})
      << "the clock must sit one millisecond short of the boundary";
  const FireAuthorisation one_millisecond_early =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_FALSE(one_millisecond_early.granted().has_value())
      << "at cool-down minus 1 ms the period has not elapsed";
  EXPECT_EQ(one_millisecond_early.refusal_reason(), FireRefusal::COOLING_DOWN);

  clock.advance_by(milliseconds{1});
  ASSERT_EQ(clock.now().since_epoch - fired_at, cool_down)
      << "the clock must now read exactly the cool-down boundary";
  const FireAuthorisation exactly_on_the_boundary =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_TRUE(exactly_on_the_boundary.granted().has_value())
      << "elapsed >= duration: exactly the cool-down duration later is permitted";
}

// Verifies: REQ-SAF-005 — "a sequence of confirmed targets arriving faster
// than the configured rate produces no more than the configured number of fire
// commands per minute", and the window is a sliding one on the injected clock.
TEST(SafetyPolicyRateLimit, AllowsNoMoreThanTheConfiguredEngagementsPerMinute) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  const Configuration configuration = calibrated_configuration();
  SafetyPolicy policy{configuration, clock};

  // Six bursts, each well past the cool-down, all inside one minute.
  for (std::uint32_t burst = 0; burst < configuration.safety.max_engagements_per_minute; ++burst) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
    ASSERT_TRUE(authorisation.granted().has_value()) << "burst " << burst << " was refused";
    policy.record_fire_sent();
    clock.advance_by(milliseconds{3000});
  }

  const FireAuthorisation seventh =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_FALSE(seventh.granted().has_value())
      << "a seventh burst inside the same minute must be refused";
  EXPECT_EQ(seventh.refusal_reason(), FireRefusal::RATE_LIMIT_REACHED);

  // Far enough on that the earliest burst has left the one-minute window.
  clock.advance_by(milliseconds{45001});
  const FireAuthorisation later =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_TRUE(later.granted().has_value())
      << "the rate window slides: bursts older than a minute no longer count";
}

// Verifies: REQ-SAF-005 — "a transmitted burst **exactly** one minute old no
// longer counts towards the rate limit, so the engagement it occupied becomes
// available again at that instant".
//
// The window of length *D* ending at *now* is half-open — it contains every
// instant strictly later than *now - D* — so a burst exactly *D* old has left
// it (ADR-0014, Q18). The pair of assertions brackets nothing: one lands one
// millisecond before the roll, the other exactly on it.
//
// The six bursts are placed 3 s apart, so the cool-down cannot be the reason
// for either answer, and the refusal reason is asserted to prove that the
// refusal being observed is the rate limit and not something else.
TEST(SafetyPolicyRateLimit, ABurstExactlyOneMinuteOldHasLeftTheRateWindow) {
  constexpr milliseconds rate_window{60000};
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  const Configuration configuration = calibrated_configuration();
  SafetyPolicy policy{configuration, clock};

  const milliseconds oldest_burst_at = clock.now().since_epoch;
  for (std::uint32_t burst = 0; burst < configuration.safety.max_engagements_per_minute; ++burst) {
    ASSERT_TRUE(policy.authorise_fire(fire_intent(), straight_ahead, healthy_link)
                    .granted()
                    .has_value())
        << "burst " << burst << " was refused while setting the window up";
    policy.record_fire_sent();
    if (burst + 1 < configuration.safety.max_engagements_per_minute) {
      clock.advance_by(milliseconds{3000});
    }
  }

  // Move to exactly one millisecond before the oldest burst leaves the window.
  const milliseconds elapsed = clock.now().since_epoch - oldest_burst_at;
  clock.advance_by(rate_window - milliseconds{1} - elapsed);
  ASSERT_EQ(clock.now().since_epoch - oldest_burst_at, rate_window - milliseconds{1})
      << "the oldest burst must be one millisecond short of a minute old";
  const FireAuthorisation still_full =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_FALSE(still_full.granted().has_value())
      << "at 59 999 ms the oldest burst is still inside the window and the rate is full";
  EXPECT_EQ(still_full.refusal_reason(), FireRefusal::RATE_LIMIT_REACHED)
      << "the refusal must be the rate limit, not the cool-down: "
         "the most recent burst is long past";

  clock.advance_by(milliseconds{1});
  ASSERT_EQ(clock.now().since_epoch - oldest_burst_at, rate_window)
      << "the oldest burst must now be exactly one minute old";
  const FireAuthorisation window_rolled =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_TRUE(window_rolled.granted().has_value())
      << "the window is half-open: a burst exactly one minute old no longer counts";
}

// Verifies: REQ-SAF-007 — "authorising is not firing": `authorise_fire` records
// nothing, so a burst that was never transmitted starts no cool-down. Two
// consecutive authorisations with nothing sent in between are both granted.
TEST(SafetyPolicyRateLimit, AuthorisingDoesNotItselfStartTheCoolDown) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  SafetyPolicy policy{calibrated_configuration(), clock};

  const FireAuthorisation first =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  const FireAuthorisation second =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);

  EXPECT_TRUE(first.granted().has_value());
  EXPECT_TRUE(second.granted().has_value())
      << "a permission that was never acted on must not consume an engagement";
}

// Verifies: REQ-SAF-007 — "a simulated link that rejects a fire command still
// starts the cool-down": the command left the Raspberry Pi, so it counts,
// whatever came back (ADR-0011).
TEST(SafetyPolicyRateLimit, ATransmittedButRejectedBurstStillStartsTheCoolDown) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  SafetyPolicy policy{calibrated_configuration(), clock};

  ASSERT_TRUE(policy.authorise_fire(fire_intent(), straight_ahead, healthy_link)
                  .granted()
                  .has_value());
  // The caller transmitted the command and the device rejected it; the caller
  // records it regardless.
  policy.record_fire_sent();

  const FireAuthorisation next =
      policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
  EXPECT_FALSE(next.granted().has_value());
  EXPECT_EQ(next.refusal_reason(), FireRefusal::COOLING_DOWN);
}

// Verifies: REQ-SAF-007 — "a burst refused before transmission — empty
// envelope, exclusion zone, rate limit — starts no cool-down and consumes no
// engagement".
TEST(SafetyPolicyRateLimit, ABurstRefusedBeforeTransmissionStartsNoCoolDown) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  Configuration configuration = calibrated_configuration();
  configuration.safety.exclusion_zone = exclusion_zone(-10.0, 10.0, 0.0, 20.0);
  SafetyPolicy policy{configuration, clock};

  const ServoAngles inside_zone{.x = Angle{0.0}, .y = Angle{10.0}};
  const FireAuthorisation refused =
      policy.authorise_fire(fire_intent(), inside_zone, healthy_link);
  ASSERT_FALSE(refused.granted().has_value());
  ASSERT_EQ(refused.refusal_reason(), FireRefusal::EXCLUSION_ZONE);

  const ServoAngles outside_zone{.x = Angle{60.0}, .y = Angle{35.0}};
  const FireAuthorisation next =
      policy.authorise_fire(fire_intent(), outside_zone, healthy_link);
  EXPECT_TRUE(next.granted().has_value())
      << "a burst that never left the Raspberry Pi must not consume an engagement";
}

// Verifies: REQ-DEV-002 — the policy reads time only through the injected
// clock: with the clock held still, the cool-down never elapses however many
// times it is asked.
TEST(SafetyPolicyRateLimit, ReadsTimeOnlyThroughTheInjectedClock) {
  ManualClock clock;
  SimulatedActuatorLink healthy_link{clock};
  SafetyPolicy policy{calibrated_configuration(), clock};

  ASSERT_TRUE(policy.authorise_fire(fire_intent(), straight_ahead, healthy_link)
                  .granted()
                  .has_value());
  policy.record_fire_sent();

  for (int attempt = 0; attempt < 50; ++attempt) {
    const FireAuthorisation authorisation =
        policy.authorise_fire(fire_intent(), straight_ahead, healthy_link);
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
  SimulatedActuatorLink healthy_link{clock};
  Configuration configuration = calibrated_configuration();
  configuration.safety.exclusion_zone = exclusion_zone(-10.0, 10.0, 0.0, 20.0);
  SafetyPolicy policy{configuration, clock};

  const ServoAngles inside_zone{.x = Angle{0.0}, .y = Angle{10.0}};
  EXPECT_TRUE(policy.authorise_aim(inside_zone).has_value());

  ASSERT_TRUE(policy.authorise_fire(fire_intent(), ServoAngles{.x = Angle{60.0}, .y = Angle{35.0}},
                                    healthy_link)
                  .granted()
                  .has_value());
  policy.record_fire_sent();

  EXPECT_TRUE(policy.authorise_aim(inside_zone).has_value())
      << "a cooling-down rig may still point at a bird";
}

}  // namespace
