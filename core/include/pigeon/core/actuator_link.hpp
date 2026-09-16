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

/// The outcome of one attempt to talk to the actuator system.
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
  /// The command was transmitted and accepted.
  OK,
  /// The link is not open, or has been lost (`REQ-COM-002`).
  UNAVAILABLE,
  /// The link is open but the exchange failed: a write error, a framing
  /// error, a malformed or absent reply (`REQ-COM-001`).
  TRANSPORT_FAILURE,
  /// The device understood the command and refused it — angles outside its
  /// own envelope, or a burst longer than its own bound (`REQ-AIM-002`,
  /// `REQ-SAF-001`). A refusal is a working safety limit, not a fault.
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

  /// Whether the link is currently usable.
  ///
  /// A hint, not a guarantee: the link may still fail on the next command, so
  /// callers must handle a failing `LinkStatus` regardless. It exists so that
  /// an engagement is not begun over a link already known to be down
  /// (`REQ-COM-002`).
  [[nodiscard]] virtual bool is_available() const noexcept = 0;
};

}  // namespace pigeon::core
