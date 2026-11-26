#pragma once

#include <chrono>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/clock.hpp"
#include "pigeon/raspberry/byte_stream.hpp"

namespace pigeon::raspberry {

/// The host's timing parameters for the device protocol
/// (`docs/architecture/device-protocol.md`, section 9). The defaults are the
/// specification's; they are parameters because the first hardware run may move
/// them (ADR-0021).
struct LinkTimings {
  /// How long a command waits for its reply before the exchange faults.
  std::chrono::milliseconds reply_timeout{100};
  /// How long without a valid device message before the link is `UNAVAILABLE`.
  std::chrono::milliseconds silence_timeout{1500};
  /// A reader that finds more than this has passed since it last ran discards
  /// what the line buffered meanwhile instead of stamping it.
  std::chrono::milliseconds stall_bound{200};
};

/// The host side of the device protocol, behind `ActuatorLink`
/// (`REQ-COM-001`, ADR-0021).
///
/// Poll-driven and single-threaded: it reads the line only when told to, from
/// an injected `ByteStream`, and tells the time only from an injected clock, so
/// that every timing in the specification can be crossed in a test without
/// waiting (`REQ-DEV-002`).
///
/// **Work in progress.** Only `health()` before the device is heard is
/// implemented. The `send_*` operations refuse everything until the exchanges
/// are built, which fails closed: only `LinkStatus::OK` permits a burst.
class SerialActuatorLink final : public pigeon::core::ActuatorLink {
 public:
  /// Sends nothing: nothing is known about the device until it has been heard
  /// (`REQ-COM-003`). `line` and `clock` must outlive the link.
  SerialActuatorLink(ByteStream& line, const pigeon::core::MonotonicClock& clock,
                     LinkTimings timings = {});

  [[nodiscard]] pigeon::core::LinkStatus send_aiming_command(
      const pigeon::core::AimingCommand& command) override;
  [[nodiscard]] pigeon::core::LinkStatus send_fire_command(
      const pigeon::core::FireCommand& command) override;
  [[nodiscard]] pigeon::core::LinkStatus send_safe_state_command() override;

  /// Answered from state alone: transmits nothing and blocks on nothing
  /// (`REQ-COM-003`).
  [[nodiscard]] pigeon::core::LinkStatus health() const noexcept override;

 private:
  [[maybe_unused]] ByteStream& line_;
  [[maybe_unused]] const pigeon::core::MonotonicClock& clock_;
  [[maybe_unused]] LinkTimings timings_;
};

}  // namespace pigeon::raspberry
