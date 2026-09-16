pigeon_add_test(core_build_info_test
  SOURCES build_info_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_geometry_test
  SOURCES geometry_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_detection_test
  SOURCES detection_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_track_association_test
  SOURCES track_association_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_track_selection_test
  SOURCES track_selection_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_target_state_test
  SOURCES target_state_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_target_state_exhaustive_test
  SOURCES target_state_exhaustive_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_aiming_test
  SOURCES aiming_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_configuration_test
  SOURCES configuration_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_safety_policy_test
  SOURCES safety_policy_test.cpp
  LIBS pigeon::core
)

pigeon_add_test(core_engagement_scenario_test
  SOURCES engagement_scenario_test.cpp
  LIBS pigeon::core
)

# The test doubles, fixtures and scenario driver are headers included by the
# suites above. Listed so that IDEs show them alongside the tests; they are
# compiled through the test sources that include them.
target_sources(core_engagement_scenario_test
  PRIVATE
    fixtures/rgb888_fixtures.hpp
    support/detection_builders.hpp
    support/engagement_loop.hpp
    support/frame_driver.hpp
    support/manual_clock.hpp
    support/simulated_actuator_link.hpp
    support/simulated_detectors.hpp
    support/test_configuration.hpp
)

# Test doubles and fixtures are included as "support/..." and "fixtures/..."
# from any depth, so this directory is on the include path of every suite here.
foreach(target
    core_geometry_test
    core_detection_test
    core_track_association_test
    core_track_selection_test
    core_target_state_test
    core_target_state_exhaustive_test
    core_aiming_test
    core_configuration_test
    core_safety_policy_test
    core_engagement_scenario_test)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
