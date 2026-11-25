#pragma once

// Helpers for driving a pigeon::protocol::Decoder from a test.

#include <cstdint>
#include <vector>

#include "pigeon/protocol/protocol.hpp"
#include "support/vectors.hpp"

namespace pigeon::protocol_test {

using Statuses = std::vector<pigeon::protocol::DecodeStatus>;

/// Pushes every byte, in order, and returns the status each one produced.
inline Statuses push_all(pigeon::protocol::Decoder& decoder, const Bytes& bytes) {
  Statuses statuses;
  statuses.reserve(bytes.size());
  for (const std::uint8_t byte : bytes) {
    statuses.push_back(decoder.push(byte));
  }
  return statuses;
}

/// `count` copies of `status`.
inline Statuses repeat(pigeon::protocol::DecodeStatus status, std::size_t count) {
  return Statuses(count, status);
}

/// The statuses a valid packet of `size` bytes produces: NEED_MORE for every
/// byte but the last, which is PACKET_READY.
inline Statuses accepted(std::size_t size) {
  Statuses statuses = repeat(pigeon::protocol::DecodeStatus::NEED_MORE, size - 1);
  statuses.push_back(pigeon::protocol::DecodeStatus::PACKET_READY);
  return statuses;
}

/// `first` followed by `second`.
template <typename T>
std::vector<T> concat(std::vector<T> first, const std::vector<T>& second) {
  first.insert(first.end(), second.begin(), second.end());
  return first;
}

/// The bytes `encode` produced, as a vector, so they compare against a vector.
inline Bytes bytes_of(const pigeon::protocol::EncodedPacket& packet) {
  return {&packet.bytes[0], &packet.bytes[0] + packet.size};
}

}  // namespace pigeon::protocol_test
