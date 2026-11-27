# Hand-run tools. Not tests (ADR-0019 §9, ADR-0021).
#
# Neither tool is **ever registered with CTest**, so neither can run in CI by
# construction (REQ-DEV-003): each needs hardware or a device to measure.
#
# - The throughput benchmark needs the real detector, so it is built only when ncnn
#   is present. REQ-DET-003 is measured by running it by hand on a Raspberry Pi 3B
#   and recording the result in docs/architecture/.
# - The link check needs only the host link, so it is built on every machine. It is
#   run by hand against a real Arduino (ADR-0021 follow-up 3). Its logic is tested
#   in tests/raspberry/link/; only `main` and the real port are not.

if(TARGET pigeon::raspberry)
  add_executable(pigeon_detector_benchmark detector_benchmark.cpp)

  target_link_libraries(pigeon_detector_benchmark
    PRIVATE
      pigeon::compile_options
      pigeon::raspberry
  )
endif()

add_executable(pigeon_link_check link_check.cpp)

target_link_libraries(pigeon_link_check
  PRIVATE
    pigeon::compile_options
    pigeon::raspberry_link
)
