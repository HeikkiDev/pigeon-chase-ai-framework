# pigeon_add_test(<name> SOURCES <files...> [LIBS <targets...>] [LABELS <labels>])
#
# Registers a GoogleTest binary with CTest. Every test target automatically
# links the shared warning configuration so tests are held to the same
# standard as production code.
#
# LABELS defaults to "unit". A suite that needs to be selectable on its own —
# tests/raspberry/ carries "unit;model" so the real detector can be run or
# excluded deliberately (ADR-0019) — passes its own set.
function(pigeon_add_test name)
  cmake_parse_arguments(ARG "" "LABELS" "SOURCES;LIBS" ${ARGN})

  if(NOT ARG_LABELS)
    set(ARG_LABELS "unit")
  endif()

  add_executable(${name} ${ARG_SOURCES})

  target_link_libraries(${name}
    PRIVATE
      pigeon::compile_options
      GTest::gmock_main
      ${ARG_LIBS}
  )

  gtest_discover_tests(${name}
    PROPERTIES LABELS "${ARG_LABELS}"
    DISCOVERY_MODE PRE_TEST
  )
endfunction()

add_subdirectory(core)

# The real detector's suite exists only when ncnn does; the top-level
# CMakeLists.txt owns that probe and fails configuration under
# PIGEON_STRICT_TOOLS=1, so CI cannot skip this silently (ADR-0019 §7).
if(TARGET pigeon::raspberry)
  add_subdirectory(raspberry)
endif()

# The gate-script suite needs a POSIX shell. It is required, not optional: the
# enforcement scripts are part of the product's evidence chain.
find_program(BASH_PROGRAM bash REQUIRED)
add_subdirectory(scripts)
