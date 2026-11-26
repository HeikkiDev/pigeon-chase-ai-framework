# The device wire codec's suite (ADR-0021).
#
# The codec needs no hardware and reads no clock, so every test here belongs in
# the default run. Vectors are the normative ones of
# docs/architecture/device-protocol.md, section 10.

pigeon_add_test(protocol_bytes_test
  SOURCES bytes_test.cpp
  LIBS pigeon::protocol
)

pigeon_add_test(protocol_crc16_test
  SOURCES crc16_test.cpp
  LIBS pigeon::protocol
)

pigeon_add_test(protocol_cobs_test
  SOURCES cobs_test.cpp
  LIBS pigeon::protocol
)

pigeon_add_test(protocol_message_test
  SOURCES message_test.cpp
  LIBS pigeon::protocol
)

pigeon_add_test(protocol_framer_test
  SOURCES framer_test.cpp
  LIBS pigeon::protocol
)
