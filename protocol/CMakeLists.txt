# The Raspberry Pi <-> Arduino wire protocol (ADR-0020, REQ-COM-001).
#
# Built as C++17 with exceptions and RTTI disabled, whatever the rest of the
# project uses, so that every `make check` proves the code still fits the
# Arduino Uno's toolchain (Q28). Those settings are PRIVATE: they apply to this
# library's own sources, not to the hosted code that links it.
add_library(pigeon_protocol
  src/protocol.cpp
  src/public_headers.cpp
)

target_sources(pigeon_protocol
  PRIVATE
    include/pigeon/protocol/protocol.hpp
)

add_library(pigeon::protocol ALIAS pigeon_protocol)

target_include_directories(pigeon_protocol
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(pigeon_protocol PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_compile_options(pigeon_protocol
  PRIVATE
    -fno-exceptions
    -fno-rtti
)

target_link_libraries(pigeon_protocol
  PRIVATE
    pigeon::compile_options
)
