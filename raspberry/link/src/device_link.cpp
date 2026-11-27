#include "pigeon/raspberry/device_link.hpp"

#include <chrono>
#include <string>

namespace pigeon::raspberry {

DeviceLink::DeviceLink(const std::string& path, DeviceLinkSettings settings)
    : port_{path, settings.serial}, link_{port_, clock_, settings.timings} {}

pigeon::core::LinkStatus DeviceLink::start(std::chrono::milliseconds max_wait) {
  link_.start_reading();
  return link_.start_up(max_wait);
}

}  // namespace pigeon::raspberry
