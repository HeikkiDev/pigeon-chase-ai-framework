# Gate-script tests.
#
# These verify the enforcement scripts in scripts/ rather than C++ behaviour,
# so they are shell scripts rather than GoogleTest binaries and are registered
# directly with CTest instead of through pigeon_add_test().
#
# They are part of the default suite on purpose: an unproven gate is worse than
# no gate, because it produces false confidence.

set(pigeon_gate_tests
  arch_check_test.sh
  trace_test.sh
  tdd_check_test.sh
)

foreach(script IN LISTS pigeon_gate_tests)
  get_filename_component(test_name "${script}" NAME_WE)

  add_test(
    NAME gate.${test_name}
    COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/${script}
  )

  set_tests_properties(gate.${test_name} PROPERTIES
    LABELS "gate"
    ENVIRONMENT "PIGEON_REPO_ROOT=${CMAKE_SOURCE_DIR}"
  )
endforeach()
