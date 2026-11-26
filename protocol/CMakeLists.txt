# The device wire codec (ADR-0021): the one implementation of the Raspberry Pi
# <-> Arduino encoding, shared by raspberry/ and the firmware.
#
# Header-only on purpose. The firmware toolchain is not chosen, so the codec is
# consumed as source, not as a library built here.
add_library(pigeon_protocol INTERFACE)
add_library(pigeon::protocol ALIAS pigeon_protocol)

target_include_directories(pigeon_protocol
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# A header that no translation unit includes is never compiled. This object
# library compiles every public header so that the compiler, -Werror and
# clang-tidy see them from the moment they are written. It is never linked.
add_library(pigeon_protocol_header_check OBJECT
  src/public_headers.cpp
  include/pigeon/protocol/bytes.hpp
  include/pigeon/protocol/cobs.hpp
  include/pigeon/protocol/crc16.hpp
  include/pigeon/protocol/framer.hpp
  include/pigeon/protocol/message.hpp
)

target_link_libraries(pigeon_protocol_header_check
  PRIVATE
    pigeon::protocol
    pigeon::compile_options
)
