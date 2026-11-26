# The host link's suite (ADR-0021, follow-up 2).
#
# The link needs no ncnn, opens no device and reads no clock of its own: its
# line and its clock are injected, so every test here belongs in the default run
# on any machine (REQ-DEV-001, REQ-DEV-002). tests/core is on the include
# path for the hand-advanced `ManualClock`.

pigeon_add_test(serial_actuator_link_health_test
  SOURCES serial_actuator_link_health_test.cpp
  LIBS pigeon::raspberry_link
)

pigeon_add_test(serial_actuator_link_safe_state_test
  SOURCES serial_actuator_link_safe_state_test.cpp
  LIBS pigeon::raspberry_link
)

pigeon_add_test(serial_actuator_link_liveness_test
  SOURCES serial_actuator_link_liveness_test.cpp
  LIBS pigeon::raspberry_link
)

pigeon_add_test(serial_actuator_link_fault_test
  SOURCES serial_actuator_link_fault_test.cpp
  LIBS pigeon::raspberry_link
)

pigeon_add_test(serial_actuator_link_aim_fire_test
  SOURCES serial_actuator_link_aim_fire_test.cpp
  LIBS pigeon::raspberry_link
)

pigeon_add_test(serial_actuator_link_startup_test
  SOURCES serial_actuator_link_startup_test.cpp
  LIBS pigeon::raspberry_link
)

pigeon_add_test(serial_actuator_link_timing_test
  SOURCES serial_actuator_link_timing_test.cpp
  LIBS pigeon::raspberry_link
)

foreach(target
    serial_actuator_link_timing_test
    serial_actuator_link_startup_test
    serial_actuator_link_aim_fire_test
    serial_actuator_link_fault_test
    serial_actuator_link_health_test
    serial_actuator_link_safe_state_test
    serial_actuator_link_liveness_test)
  target_include_directories(${target}
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_SOURCE_DIR}/tests/core
  )
endforeach()
