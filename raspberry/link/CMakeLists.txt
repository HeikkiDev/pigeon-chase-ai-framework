# The host side of the device protocol (ADR-0021, follow-up 2).
#
# Needs no ncnn, so it is added from the top-level CMakeLists.txt outside the
# ncnn gate and is built and tested on every machine (REQ-DEV-001). It lives
# under raspberry/ because it is Raspberry Pi integration, not domain logic.
add_library(pigeon_raspberry_link
  src/link_health.cpp
  src/public_headers.cpp
  src/serial_actuator_link.cpp
  src/serial_port_stream.cpp
  src/steady_clock.cpp
)

target_sources(pigeon_raspberry_link
  PRIVATE
    include/pigeon/raspberry/byte_stream.hpp
    include/pigeon/raspberry/link_health.hpp
    include/pigeon/raspberry/serial_actuator_link.hpp
    include/pigeon/raspberry/serial_port_stream.hpp
    include/pigeon/raspberry/steady_clock.hpp
)

add_library(pigeon::raspberry_link ALIAS pigeon_raspberry_link)

target_include_directories(pigeon_raspberry_link
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pigeon_raspberry_link
  PUBLIC
    pigeon::core
    pigeon::protocol
  PRIVATE
    pigeon::compile_options
)
