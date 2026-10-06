# The real detector's suite (ADR-0019 §9).
#
# Configured only when ncnn is present, which the top-level CMakeLists.txt
# decides. These tests are **not** hardware-in-the-loop: they open no device
# and read no clock, so they belong in the default run (the hardware-only
# requirement is about devices, not about size). The `model` label exists so
# they can be selected or excluded deliberately:
#
#     ctest --preset macos-debug -L model
#     ctest --preset macos-debug -LE model
#
# Paths to the vendored model bundle and to the fixtures are compile
# definitions, so no test depends on the directory it is run from.
set(pigeon_model_bundle "${CMAKE_SOURCE_DIR}/raspberry/models/yolo-fastest-1.1")

set(pigeon_raspberry_tests
  ncnn_detector_load_test
  ncnn_detector_detect_test
  ncnn_detector_geometry_test
)

pigeon_add_test(ncnn_detector_load_test
  SOURCES ncnn_detector_load_test.cpp
  LIBS pigeon::raspberry
  LABELS "unit;model"
)

pigeon_add_test(ncnn_detector_detect_test
  SOURCES ncnn_detector_detect_test.cpp
  LIBS pigeon::raspberry
  LABELS "unit;model"
)

pigeon_add_test(ncnn_detector_geometry_test
  SOURCES ncnn_detector_geometry_test.cpp
  LIBS pigeon::raspberry
  LABELS "unit;model"
)

# The fixture loader and the settings builders are headers included by the
# suites above. Listed so that IDEs show them alongside the tests.
target_sources(ncnn_detector_detect_test
  PRIVATE
    support/model_fixture.hpp
    support/ppm_fixture.hpp
)

foreach(target IN LISTS pigeon_raspberry_tests)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  target_compile_definitions(${target}
    PRIVATE
      PIGEON_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
      PIGEON_TEST_MODEL_PARAM="${pigeon_model_bundle}/yolo-fastest-1.1.param"
      PIGEON_TEST_MODEL_WEIGHTS="${pigeon_model_bundle}/yolo-fastest-1.1.bin"
  )
endforeach()
