#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

#include "pigeon/core/actuator_link.hpp"

namespace pigeon::raspberry {

/// What the host link's health is judged from (`device-protocol.md` section 7):
/// everything except the time now.
struct LinkHealthState {
  /// Whether the line is open.
  bool open{false};
  /// When a valid device message was last read, if ever.
  std::optional<std::chrono::milliseconds> last_heard;
  /// An exchange ended in a fault and none has been answered since.
  bool faulted{false};
  /// An `AIM` or `FIRE` was refused and none has been accepted since.
  bool rejected{false};
};

/// The health rule of section 7, the one place it is written (ADR-0023): the
/// first of `UNAVAILABLE` (closed, never heard, or silent for `silence_timeout`,
/// judged as elapsed >= timeout), `TRANSPORT_FAILURE`, `REJECTED`, else `OK`.
[[nodiscard]] pigeon::core::LinkStatus health_from(
    const LinkHealthState& state, std::chrono::milliseconds now,
    std::chrono::milliseconds silence_timeout) noexcept;

/// The state in one word, so that a reader on another thread takes it whole from
/// a `std::atomic<std::uint64_t>`, which is lock-free (ADR-0023).
///
/// The time is kept to 60 bits of milliseconds. One before the epoch is kept as the
/// epoch, which only makes the silence look longer; one too late to fit is kept as
/// never heard. Either way the snapshot errs towards `UNAVAILABLE`, never `OK`.
[[nodiscard]] std::uint64_t pack(const LinkHealthState& state) noexcept;

/// The inverse of `pack`.
[[nodiscard]] LinkHealthState unpack(std::uint64_t word) noexcept;

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "the health snapshot must be readable without a lock (ADR-0023)");

}  // namespace pigeon::raspberry
