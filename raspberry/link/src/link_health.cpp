#include "pigeon/raspberry/link_health.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

namespace pigeon::raspberry {

using pigeon::core::LinkStatus;

namespace {

constexpr std::uint64_t open_bit = 1U << 0U;
constexpr std::uint64_t heard_bit = 1U << 1U;
constexpr std::uint64_t faulted_bit = 1U << 2U;
constexpr std::uint64_t rejected_bit = 1U << 3U;
constexpr unsigned time_shift = 4;
constexpr std::uint64_t latest_time = (std::uint64_t{1} << (64U - time_shift)) - 1U;

}  // namespace

LinkStatus health_from(const LinkHealthState& state, std::chrono::milliseconds now,
                       std::chrono::milliseconds silence_timeout) noexcept {
  // A line that has just opened is UNAVAILABLE, not OK: nothing is known about the
  // device until it has been heard.
  if (!state.open || !state.last_heard.has_value()) {
    return LinkStatus::UNAVAILABLE;
  }
  if (now - *state.last_heard >= silence_timeout) {
    return LinkStatus::UNAVAILABLE;
  }
  if (state.faulted) {
    return LinkStatus::TRANSPORT_FAILURE;
  }
  if (state.rejected) {
    return LinkStatus::REJECTED;
  }
  return LinkStatus::OK;
}

std::uint64_t pack(const LinkHealthState& state) noexcept {
  std::uint64_t word = 0;
  if (state.open) {
    word |= open_bit;
  }
  if (state.faulted) {
    word |= faulted_bit;
  }
  if (state.rejected) {
    word |= rejected_bit;
  }
  if (state.last_heard.has_value()) {
    // A time that cannot be packed as it is, before the epoch or too late for 60
    // bits, is kept as never heard: that reads UNAVAILABLE whatever the clock says.
    const auto count = state.last_heard->count();
    if (count >= 0 && std::cmp_less_equal(count, latest_time)) {
      word |= heard_bit | (static_cast<std::uint64_t>(count) << time_shift);
    }
  }
  return word;
}

LinkHealthState unpack(std::uint64_t word) noexcept {
  LinkHealthState state;
  state.open = (word & open_bit) != 0;
  state.faulted = (word & faulted_bit) != 0;
  state.rejected = (word & rejected_bit) != 0;
  if ((word & heard_bit) != 0) {
    state.last_heard = std::chrono::milliseconds{static_cast<std::int64_t>(word >> time_shift)};
  }
  return state;
}

}  // namespace pigeon::raspberry
