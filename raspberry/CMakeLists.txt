# The Raspberry Pi integration layer (ADR-0019).
#
# Configured only when ncnn is present; the top-level CMakeLists.txt owns that
# probe. Optional locally, mandatory in CI under PIGEON_STRICT_TOOLS=1, so a
# clean macOS checkout without ncnn still builds and tests (REQ-DEV-001).
#
# Declarations only for now: the implementation of ncnn_detector.hpp is the
# implementation-engineer's work and belongs in src/ncnn_detector.cpp, the only
# file in the repository permitted to include <ncnn/...> (REQ-DET-002).

add_library(pigeon_raspberry
  # Compiles every public header, so an interface-only change is still checked
  # by the compiler and by clang-tidy. See the comment in the file.
  src/public_headers.cpp
)

# Listed so that IDEs show the interfaces as part of the target. The headers
# are compiled through src/public_headers.cpp, not through this list.
target_sources(pigeon_raspberry
  PRIVATE
    include/pigeon/raspberry/ncnn_detector.hpp
)

add_library(pigeon::raspberry ALIAS pigeon_raspberry)

target_include_directories(pigeon_raspberry
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pigeon_raspberry
  PUBLIC
    pigeon::core
  PRIVATE
    pigeon::compile_options
    ncnn
)
