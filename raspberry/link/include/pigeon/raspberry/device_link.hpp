#pragma once

#include <chrono>
#include <string>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/raspberry/concurrent_actuator_link.hpp"
#include "pigeon/raspberry/serial_actuator_link.hpp"
#include "pigeon/raspberry/serial_port_stream.hpp"
#include "pigeon/raspberry/steady_clock.hpp"

namespace pigeon::raspberry {

/// How the device link is set up. The defaults are the specification's.
struct DeviceLinkSettings {
  SerialSettings serial;
  LinkTimings timings;
};

/// The Raspberry Pi's whole connection to the actuator system, in one object
/// (ADR-0021 follow-up 3, ADR-0023): the serial port, the real clock, and the host
/// link that reads the port on its own thread, constructed in the order they
/// depend on one another.
///
/// What the application constructs, calls `start()` on, and hands to `core/` as its
/// `ActuatorLink`. If the port cannot be opened, the object still exists: it
/// reports why, its link reads `UNAVAILABLE`, and every command is refused with
/// nothing transmitted (`REQ-COM-002`).
///
/// Destroying it stops the reader, sends `SAFE_STATE` if the port is open
/// (`REQ-SAF-004`), and then closes the port.
class DeviceLink {
 public:
  explicit DeviceLink(const std::string& path, DeviceLinkSettings settings = {});

  DeviceLink(const DeviceLink&) = delete;
  DeviceLink& operator=(const DeviceLink&) = delete;
  DeviceLink(DeviceLink&&) = delete;
  DeviceLink& operator=(DeviceLink&&) = delete;

  ~DeviceLink() = default;

  /// Whether the port was opened.
  [[nodiscard]] bool is_open() const noexcept { return port_.is_open(); }

  /// Why the port could not be opened (`errno`), or zero if it could.
  [[nodiscard]] int open_error() const noexcept { return port_.open_error(); }

  /// Starts the reader thread, then brings the link up: waits to hear the device
  /// and sends `SAFE_STATE` until one is accepted, giving up after `max_wait`
  /// (`SerialActuatorLink::start_up`). `UNAVAILABLE` if the port is not open.
  [[nodiscard]] pigeon::core::LinkStatus start(std::chrono::milliseconds max_wait);

  /// The link to hand to `core/`. Lives as long as this object.
  [[nodiscard]] pigeon::core::ActuatorLink& link() noexcept { return link_; }

 private:
  // The order is the dependency order, and the reverse is the shutdown order: the
  // link (and its reader) is destroyed first, sending SAFE_STATE while the port and
  // clock it uses still exist.
  SerialPortStream port_;
  SteadyClock clock_;
  ConcurrentActuatorLink link_;
};

}  // namespace pigeon::raspberry
