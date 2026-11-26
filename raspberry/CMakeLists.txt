# The Raspberry Pi integration layer (ADR-0019).
#
# Configured only when ncnn is present; the top-level CMakeLists.txt owns that
# probe. Optional locally, mandatory in CI under PIGEON_STRICT_TOOLS=1, so a
# clean macOS checkout without ncnn still builds and tests (REQ-DEV-001).
#
# src/ncnn_detector.cpp is the only file in the repository permitted to include
# <ncnn/...> (REQ-DET-002).

# The vendored model bundle is a build input (ADR-0019 §8). Verifying its
# digests at configure time turns a corrupted or swapped model into a build
# failure rather than into a silent change of what the system shoots at.
set(pigeon_model_dir "${CMAKE_CURRENT_SOURCE_DIR}/models/yolo-fastest-1.1")
set(pigeon_model_checksums "${pigeon_model_dir}/checksums.sha256")

if(NOT EXISTS "${pigeon_model_checksums}")
  message(FATAL_ERROR "model bundle checksum file missing: ${pigeon_model_checksums}")
endif()

# Re-run configuration when the recorded digests change.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pigeon_model_checksums}")

file(STRINGS "${pigeon_model_checksums}" pigeon_model_checksum_lines)
foreach(line IN LISTS pigeon_model_checksum_lines)
  if(line MATCHES "^([0-9a-fA-F]+)[ \t]+[*]?(.+)$")
    set(expected_digest "${CMAKE_MATCH_1}")
    set(model_file "${pigeon_model_dir}/${CMAKE_MATCH_2}")

    if(NOT EXISTS "${model_file}")
      message(FATAL_ERROR "model bundle file missing: ${model_file}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${model_file}")
    file(SHA256 "${model_file}" actual_digest)

    if(NOT actual_digest STREQUAL expected_digest)
      message(FATAL_ERROR
        "model bundle checksum mismatch for ${model_file}:\n"
        "  expected ${expected_digest}\n"
        "  actual   ${actual_digest}\n"
        "Restore the vendored model, or update checksums.sha256 and SOURCE.md "
        "deliberately (ADR-0019 section 8).")
    endif()
  else()
    message(FATAL_ERROR "unparseable line in ${pigeon_model_checksums}: ${line}")
  endif()
endforeach()

message(STATUS "model bundle verified: ${pigeon_model_dir}")

add_library(pigeon_raspberry
  # Compiles every public header, so an interface-only change is still checked
  # by the compiler and by clang-tidy. See the comment in the file.
  src/public_headers.cpp
  src/ncnn_detector.cpp
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

# ncnn's package config exports <prefix>/include/ncnn, so its own headers are
# included as <net.h>. The parent directory is added as well so this module can
# write <ncnn/net.h>: the qualified form is what ADR-0019 names as the marker
# of the one translation unit allowed to see the runtime, and what the
# requested scripts/arch-check.sh rule would look for.
get_target_property(pigeon_ncnn_includes ncnn INTERFACE_INCLUDE_DIRECTORIES)
foreach(include_dir IN LISTS pigeon_ncnn_includes)
  if(include_dir MATCHES "/ncnn$")
    cmake_path(GET include_dir PARENT_PATH pigeon_ncnn_parent_include)
    target_include_directories(pigeon_raspberry SYSTEM PRIVATE "${pigeon_ncnn_parent_include}")
  endif()
endforeach()
