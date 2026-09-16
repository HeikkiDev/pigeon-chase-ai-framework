add_library(pigeon_core
  src/build_info.cpp
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
