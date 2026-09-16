add_library(pigeon_core
  src/build_info.cpp
  # Compiles every public header, so an interface-only change is still checked
  # by the compiler and by clang-tidy. See the comment in the file.
  src/public_headers.cpp
)

# Listed so that IDEs show the interfaces as part of the target. The headers are
# compiled through src/public_headers.cpp, not through this list.
target_sources(pigeon_core
  PRIVATE
    include/pigeon/core/actuator_link.hpp
    include/pigeon/core/aiming.hpp
    include/pigeon/core/build_info.hpp
    include/pigeon/core/clock.hpp
    include/pigeon/core/configuration.hpp
    include/pigeon/core/detection.hpp
    include/pigeon/core/detector.hpp
    include/pigeon/core/frame.hpp
    include/pigeon/core/geometry.hpp
    include/pigeon/core/safety_policy.hpp
    include/pigeon/core/target_state.hpp
    include/pigeon/core/track.hpp
)

add_library(pigeon::core ALIAS pigeon_core)

target_include_directories(pigeon_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pigeon_core
  PRIVATE
    pigeon::compile_options
)

target_compile_definitions(pigeon_core
  PRIVATE
    PIGEON_VERSION_STRING="${PROJECT_VERSION}"
)
