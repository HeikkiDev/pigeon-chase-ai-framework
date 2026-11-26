#include "pigeon/raspberry/concurrent_actuator_link.hpp"

#include <atomic>
#include <chrono>
#include <mutex>

#include "pigeon/raspberry/link_health.hpp"

namespace pigeon::raspberry {

using pigeon::core::LinkStatus;

ConcurrentActuatorLink::ConcurrentActuatorLink(ByteStream& line,
                                               const pigeon::core::MonotonicClock& clock,
                                               LinkTimings timings)
    : clock_{clock},
      silence_timeout_{timings.silence_timeout},
      link_{line, clock, timings},
      published_{pack(link_.health_state())} {}

LinkStatus ConcurrentActuatorLink::start_up(std::chrono::milliseconds max_wait) {
  const std::scoped_lock lock{mutex_};
  const LinkStatus status = link_.start_up(max_wait);
  publish();
  return status;
}

LinkStatus ConcurrentActuatorLink::send_aiming_command(const pigeon::core::AimingCommand& command) {
  const std::scoped_lock lock{mutex_};
  const LinkStatus status = link_.send_aiming_command(command);
  publish();
  return status;
}

LinkStatus ConcurrentActuatorLink::send_fire_command(const pigeon::core::FireCommand& command) {
  const std::scoped_lock lock{mutex_};
  const LinkStatus status = link_.send_fire_command(command);
  publish();
  return status;
}

LinkStatus ConcurrentActuatorLink::send_safe_state_command() {
  const std::scoped_lock lock{mutex_};
  const LinkStatus status = link_.send_safe_state_command();
  publish();
  return status;
}

LinkStatus ConcurrentActuatorLink::health() const noexcept {
  // No lock: the published word is read whole, and the rule is the link's own.
  return health_from(unpack(published_.load(std::memory_order_acquire)), clock_.now().since_epoch,
                     silence_timeout_);
}

void ConcurrentActuatorLink::poll_once() noexcept {
  const std::scoped_lock lock{mutex_};
  link_.poll();
  publish();
}

void ConcurrentActuatorLink::publish() noexcept {
  published_.store(pack(link_.health_state()), std::memory_order_release);
}

}  // namespace pigeon::raspberry
