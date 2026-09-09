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

if(PIGEON_BUILD_TESTS)
  enable_testing()
  include(cmake/GoogleTest.cmake)
  add_subdirectory(tests)
endif()
