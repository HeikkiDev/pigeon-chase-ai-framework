#pragma once

#include <chrono>
#include <cstdint>

#include "pigeon/core/aiming.hpp"

namespace pigeon::core {

/// Instruct the actuator system to point the deterrent (`REQ-AIM-001`).
///
/// The angles are in the servo frame and are **already clamped** to the
/// mechanical envelope by the sender (`REQ-AIM-002`). The receiver clamps them
/// again: safety limits are enforced on both sides of the device boundary, so
/// that a defect on the commanding side cannot move the rig outside its
/// envelope.
struct AimingCommand {
  /// Where to point, in the servo frame.
  ServoAngles angles{};
};

/// Instruct the actuator system to activate the water actuator
/// (`REQ-SAF-001`).
///
/// The burst is self-terminating in firmware. The duration here is a request
/// bounded by the configured maximum; the Arduino applies its own bound
/// independently, so the burst ends even if the Raspberry Pi stops responding
/// mid-engagement. A duration above the firmware's bound is truncated or
/// rejected by the firmware, never obeyed.
struct FireCommand {
  /// Requested burst length. Defaults to zero — a command that does nothing —
  /// so an uninitialised command cannot open the valve.
  std::chrono::milliseconds duration{0};
};

/// The condition of the link to the actuator system.
///
/// One enumeration serves two questions, because they have the same four
/// answers: what happened to the command just sent (`send_*`), and how the
/// link is right now (`health()`). A second enumeration for the second
/// question would be a second source of truth about one thing, and would need
/// its own mapping to refusals; these values already name every condition that
/// matters (`REQ-COM-003`).
///
/// An explicit error value rather than an exception: this is a module
/// boundary, and `REQ-COM-002` requires the caller to *act* on a failure —
/// abandon the engagement, fire nothing, return to `SEARCHING` — which is
/// control flow, not an exceptional condition.
///
/// **Only `OK` permits firing.** Every other value refuses the burst, each
/// with its own `FireRefusal` (`REQ-SAF-008`, `safety_policy.hpp`, ADR-0015).
/// A status added to this enumeration must be given a refusal reason in the
/// same change; `refusal_for` is where that is enforced.
///
/// A status is about the *exchange*, not about the water: a command that was
/// transmitted counts towards the cool-down and the rate limit whatever the
/// status says afterwards (`REQ-SAF-007`, ADR-0011).
enum class LinkStatus : std::uint8_t {
  /// As an outcome: the command was transmitted and accepted. As health: the
  /// link is open and nothing is known to be wrong with it. The only value
  /// that permits a burst (`REQ-SAF-008`).
  OK,
  /// The link is not open, or has been lost (`REQ-COM-002`). As health: the
  /// transport is closed, or the device has not been heard from within the
  /// expected heartbeat interval.
  UNAVAILABLE,
  /// The link is open but the exchange failed: a write error, a framing
  /// error, a malformed or absent reply (`REQ-COM-001`). As health: the most
  /// recent exchange faulted in that way and nothing since has shown the
  /// transport to be sound again.
  TRANSPORT_FAILURE,
  /// The device understood the command and refused it — angles outside its
  /// own envelope, or a burst longer than its own bound (`REQ-AIM-002`,
  /// `REQ-SAF-001`). A refusal is a working safety limit, not a fault. As
  /// health: the device is answering and rejecting what it is told, which is a
  /// disagreement about limits and not a link to send water over.
  REJECTED,
};

/// The seam between `core/` and the actuator system (`REQ-COM-001`).
///
/// This interface *is* the device boundary. Behind it, in deployment, sits a
/// serial transport to an Arduino that drives two servos and a water actuator;
/// in the default test suite sits a simulated link that can be told to fail on
/// demand, because `REQ-COM-002` has to be provable without unplugging
/// anything.
///
/// It is a **protocol**, not merely a C++ type: the two sides run on different
/// physical devices, so the obligations below hold for the firmware as much as
/// for any C++ implementation. The wire encoding that carries these operations
/// is specified separately in `docs/architecture/` (`REQ-COM-001`) and is not
/// yet written.
///
/// Obligations of every implementation:
/// * **Safe on construction and on destruction.** The water actuator is
///   inactive when the link comes up and inactive when it goes away
///   (`REQ-SAF-004`). Destruction must not leave a valve open.
/// * **Re-check what it is asked to do.** Angles are clamped again on receipt
///   and a burst longer than the device's own bound is refused
///   (`REQ-AIM-002`, `REQ-SAF-001`). The receiving side does not trust the
///   sending side.
/// * **Report failure, never hide it.** Every operation returns a
///   `LinkStatus`; a failure is never swallowed and never signalled by an
///   exception crossing this boundary.
/// * **Self-terminate a burst.** A fire command activates the actuator for at
///   most the bounded duration with no further command required, and with no
///   dependence on the caller still being alive.
/// * **Answer `health()` without talking to the device.** The query is
///   answered from what the commanding side already knows. It transmits
///   nothing, blocks on nothing and changes nothing (`REQ-COM-003`).
/// * **Keep the commanding side's picture current.** Every command is answered
///   by a reply that distinguishes acceptance from refusal, and the device
///   emits a periodic heartbeat, so `health()` can degrade without a fire
///   command being sent to discover the fault (`REQ-COM-001`, `REQ-COM-003`).
class ActuatorLink {
 public:
  ActuatorLink() = default;
  ActuatorLink(const ActuatorLink&) = delete;
  ActuatorLink& operator=(const ActuatorLink&) = delete;
  ActuatorLink(ActuatorLink&&) = delete;
  ActuatorLink& operator=(ActuatorLink&&) = delete;

  /// Leaves the actuator system inactive (`REQ-SAF-004`).
  virtual ~ActuatorLink() = default;

  /// Point the deterrent at the given angles.
  ///
  /// Returns `LinkStatus::OK` only if the actuator system accepted the
  /// command. Any other value abandons the engagement (`REQ-COM-002`).
  [[nodiscard]] virtual LinkStatus send_aiming_command(const AimingCommand& command) = 0;

  /// Activate the water actuator for the requested, bounded duration.
  ///
  /// Only ever called for an authorised engagement (`REQ-SAF-002`): this
  /// interface carries no authority of its own and performs no confirmation
  /// check. It is the last step, not the decision.
  [[nodiscard]] virtual LinkStatus send_fire_command(const FireCommand& command) = 0;

  /// Place the actuator system in its safe state: water actuator inactive
  /// (`REQ-SAF-004`).
  ///
  /// Called on startup, on shutdown, and whenever an engagement is abandoned.
  /// Implementations make this the cheapest and most reliable operation they
  /// offer, because it is the one that must work when nothing else does.
  [[nodiscard]] virtual LinkStatus send_safe_state_command() = 0;

  /// The link's health **right now**, before anything is sent (`REQ-COM-003`).
  ///
  /// This is the query `REQ-SAF-008` tests, and it is the reason that rule can
  /// execute at all: a burst is authorised only while this returns
  /// `LinkStatus::OK`, and each other value carries its own refusal
  /// (`refusal_for`, `safety_policy.hpp`). A boolean stood here once, which
  /// made two of the four statuses unreachable at the moment of the decision —
  /// they existed only as the outcome of an exchange already made — so the
  /// rule was correct and dead (ADR-0016).
  ///
  /// **Answered without an exchange.** Transmits nothing, blocks on nothing,
  /// changes nothing on either device; `noexcept` and `const` say so in the
  /// type system. Requiring a round trip would restore the circularity of
  /// having to talk to the device to learn whether you may talk to it. An
  /// implementation answers from its own transport state: is the port open,
  /// did the last exchange fault, has the heartbeat arrived when it was due.
  ///
  /// It is the current picture, not a promise about the future: the link may
  /// still fail on the very next command, which is why every operation returns
  /// a `LinkStatus` and why `REQ-COM-002` still applies to what comes back.
  ///
  /// There is no second, coarser query. One question about link health has one
  /// answer; two overlapping queries are two sources of truth, and two sources
  /// of truth are how they come to disagree. "Is the link usable?" is
  /// `health() == LinkStatus::OK`.
  [[nodiscard]] virtual LinkStatus health() const noexcept = 0;
};

}  // namespace pigeon::core
