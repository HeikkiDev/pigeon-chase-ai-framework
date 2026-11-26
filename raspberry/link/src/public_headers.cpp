// Infrastructure, not behaviour.
//
// Includes every public header of `pigeon::raspberry_link` so that the compiler,
// -Werror and clang-tidy parse them from the moment they are written. It
// defines no behaviour and must never acquire any. Mirrors
// `raspberry/src/public_headers.cpp`.

#include "pigeon/raspberry/byte_stream.hpp"
#include "pigeon/raspberry/concurrent_actuator_link.hpp"
#include "pigeon/raspberry/link_health.hpp"
#include "pigeon/raspberry/serial_actuator_link.hpp"
#include "pigeon/raspberry/serial_port_stream.hpp"
#include "pigeon/raspberry/steady_clock.hpp"
