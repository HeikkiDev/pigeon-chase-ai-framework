#include "pigeon/raspberry/serial_actuator_link.hpp"

namespace pigeon::raspberry {

using pigeon::core::LinkStatus;

SerialActuatorLink::SerialActuatorLink(ByteStream& line, const pigeon::core::MonotonicClock& clock,
                                       LinkTimings timings)
    : line_{line}, clock_{clock}, timings_{timings} {}

// Not built yet: refuse, so that nothing can be fired through a link that does
// not exist. Replaced test-first by the exchanges of section 6.
LinkStatus SerialActuatorLink::send_aiming_command(const pigeon::core::AimingCommand& /*command*/) {
  return LinkStatus::UNAVAILABLE;
}

LinkStatus SerialActuatorLink::send_fire_command(const pigeon::core::FireCommand& /*command*/) {
  return LinkStatus::UNAVAILABLE;
}

LinkStatus SerialActuatorLink::send_safe_state_command() { return LinkStatus::UNAVAILABLE; }

LinkStatus SerialActuatorLink::health() const noexcept {
  // Nothing has been read from the device yet, whatever the state of the line:
  // a line that has just opened is UNAVAILABLE, not OK (section 7).
  return LinkStatus::UNAVAILABLE;
}

}  // namespace pigeon::raspberry
