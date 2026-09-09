# pigeon_add_test(<name> SOURCES <files...> [LIBS <targets...>])
#
# Registers a GoogleTest binary with CTest. Every test target automatically
# links the shared warning configuration so tests are held to the same
# standard as production code.
function(pigeon_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

  add_executable(${name} ${ARG_SOURCES})

  target_link_libraries(${name}
    PRIVATE
      pigeon::compile_options
      GTest::gmock_main
      ${ARG_LIBS}
  )

  gtest_discover_tests(${name}
    PROPERTIES LABELS "unit"
    DISCOVERY_MODE PRE_TEST
  )
endfunction()

add_subdirectory(core)
