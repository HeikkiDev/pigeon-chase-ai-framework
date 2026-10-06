cmake_minimum_required(VERSION 3.24)

project(pigeon_chase
  VERSION 0.1.0
  DESCRIPTION "Anti-pigeon deterrence system and AI engineering framework"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PIGEON_BUILD_TESTS "Build the test suite" ON)
option(PIGEON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# Shared compile options consumed by every first-party target.
add_library(pigeon_compile_options INTERFACE)
add_library(pigeon::compile_options ALIAS pigeon_compile_options)

target_compile_options(pigeon_compile_options INTERFACE
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Woverloaded-virtual
)

if(PIGEON_WARNINGS_AS_ERRORS)
  target_compile_options(pigeon_compile_options INTERFACE -Werror)
endif()

add_subdirectory(core)

# The model runtime (ADR-0019). ncnn is optional locally and mandatory in CI,
# by the same PIGEON_STRICT_TOOLS precedent scripts/check.sh uses for
# clang-format and clang-tidy:
#
#   found                       -> raspberry/ and tests/raspberry/ are built
#   absent, strict tools unset  -> both are skipped, with a visible warning, so
#                                  a clean macOS checkout still builds (REQ-DEV-001)
#   absent, PIGEON_STRICT_TOOLS -> configuration fails, so CI cannot silently
#                                  run without the real detector
#
# ncnn's own package config requires the OpenMP::OpenMP_CXX imported target, and
# AppleClang only finds libomp when pointed at the keg-only Homebrew prefix. A
# missing OpenMP is therefore treated as a missing ncnn rather than as a fatal
# error from inside a third-party config file.
if(APPLE AND EXISTS "/opt/homebrew/opt/libomp")
  set(OpenMP_ROOT "/opt/homebrew/opt/libomp")
endif()

find_package(OpenMP QUIET)

if(OpenMP_CXX_FOUND)
  find_package(ncnn CONFIG QUIET)
endif()

if(ncnn_FOUND)
  message(STATUS "ncnn found (${ncnn_DIR}): building raspberry/")
  add_subdirectory(raspberry)
elseif("$ENV{PIGEON_STRICT_TOOLS}" STREQUAL "1")
  message(FATAL_ERROR
    "ncnn not found and PIGEON_STRICT_TOOLS=1. Install it with: brew install ncnn")
else()
  message(WARNING
    "ncnn not found - raspberry/ and tests/raspberry/ are skipped. "
    "CI builds them (PIGEON_STRICT_TOOLS=1). Install it with: brew install ncnn")
endif()

if(PIGEON_BUILD_TESTS)
  enable_testing()
  include(cmake/GoogleTest.cmake)
  add_subdirectory(tests)
endif()
