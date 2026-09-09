# GoogleTest is fetched at configure time so that the test suite has no
# system-wide prerequisites beyond a compiler and CMake.
include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)
