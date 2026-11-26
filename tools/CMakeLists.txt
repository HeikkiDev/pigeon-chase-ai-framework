# Hand-run tools. Not tests (ADR-0019 §9).
#
# The throughput benchmark is built only when ncnn is present — the top-level
# CMakeLists.txt owns that probe and only adds this directory then — and is
# **never registered with CTest**, so it cannot run in CI by construction
# (REQ-DEV-003). REQ-DET-003 is measured by running it by hand on a Raspberry
# Pi 3B and recording the result in docs/architecture/.

add_executable(pigeon_detector_benchmark detector_benchmark.cpp)

target_link_libraries(pigeon_detector_benchmark
  PRIVATE
    pigeon::compile_options
    pigeon::raspberry
)
