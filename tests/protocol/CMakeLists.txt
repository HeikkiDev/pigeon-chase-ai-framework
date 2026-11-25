# The wire protocol suite (ADR-0020, REQ-COM-001). Built with the project's
# own C++20 settings: only protocol/ itself is held to the Arduino's C++17.
set(pigeon_protocol_tests
  crc16_test
  encode_test
  decode_test
  decode_malformed_test
)

foreach(test IN LISTS pigeon_protocol_tests)
  pigeon_add_test(protocol_${test}
    SOURCES ${test}.cpp
    LIBS pigeon::protocol
  )
  target_include_directories(protocol_${test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
