#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/clock.hpp"
#include "pigeon/protocol/framer.hpp"
#include "pigeon/protocol/message.hpp"
#include "pigeon/raspberry/byte_stream.hpp"

namespace pigeon::raspberry {

/// The host's timing parameters for the device protocol
/// (`docs/architecture/device-protocol.md`, section 9). The defaults are the
/// specification's; they are parameters because the first hardware run may move
/// them (ADR-0021).
struct LinkTimings {
  /// How long a command waits for its reply before the exchange faults.
  std::chrono::milliseconds reply_timeout{100};
  /// How long without a valid device message before the link is `UNAVAILABLE`.
  std::chrono::milliseconds silence_timeout{1500};
  /// How long the reader goes without taking bytes off the line while it waits.
  /// Must be well under `stall_bound`, or the reader's own waits would look like
  /// stalls and the messages it woke for would be discarded.
  std::chrono::milliseconds reader_period{100};
  /// A reader that finds more than this has passed since it last ran discards
  /// what the line buffered meanwhile instead of stamping it.
  std::chrono::milliseconds stall_bound{200};
  /// The device's heartbeat interval; startup repeats `SAFE_STATE` at this pace.
  std::chrono::milliseconds heartbeat_interval{500};
};

/// The host side of the device protocol, behind `ActuatorLink`
/// (`REQ-COM-001`, ADR-0021).
///
/// Poll-driven and single-threaded: it reads the line only when told to, from
/// an injected `ByteStream`, and tells the time only from an injected clock, so
/// that every timing in the specification can be crossed in a test without
/// waiting (`REQ-DEV-002`).
///
/// **Not thread-safe.** `poll`, the `send_*` calls and `health()` share state with
/// no synchronisation, so they must be called from one thread, or serialised by
/// the owner. The caller must also `poll()` at least every reader period (100 ms)
/// between commands; nothing here calls it on a timer. Both are the business of
/// whatever drives the link once a real serial port exists (ADR-0021, follow-up 3).
///
/// **Complete for the host side of the exchanges and of `health()`**
/// (`device-protocol.md` sections 6 to 8). Not yet built: the startup sequence
/// of the serial port itself.
class SerialActuatorLink final : public pigeon::core::ActuatorLink {
 public:
  /// Sends nothing: nothing is known about the device until it has been heard
  /// (`REQ-COM-003`). `line` and `clock` must outlive the link.
  SerialActuatorLink(ByteStream& line, const pigeon::core::MonotonicClock& clock,
                     LinkTimings timings = {});

  /// Puts the actuator system in its safe state if the line is open
  /// (`REQ-SAF-004`, section 8, shutdown). Best effort: costs at most the reply
  /// timeout, is never retransmitted, and cannot report a failure to anyone. The
  /// line is closed by whoever owns the `ByteStream`, after the link is gone.
  ~SerialActuatorLink() override;

  // Not copyable or movable, like the interface: it holds references to the line
  // and the clock, and is the one thing that may put the actuator in its safe
  // state when it goes away.
  SerialActuatorLink(const SerialActuatorLink&) = delete;
  SerialActuatorLink& operator=(const SerialActuatorLink&) = delete;
  SerialActuatorLink(SerialActuatorLink&&) = delete;
  SerialActuatorLink& operator=(SerialActuatorLink&&) = delete;

  /// Brings the link up (section 8, startup): waits to hear the device, because a
  /// board that has just reset is not listening, then sends `SAFE_STATE` and
  /// repeats it once per heartbeat interval, each time as a new command, until
  /// one is accepted.
  ///
  /// Gives up at `max_wait` after the call. Returns `OK` once a `SAFE_STATE` is
  /// accepted; `UNAVAILABLE` if the line is not open or the device was not heard
  /// within the silence timeout before the deadline, in which case **nothing was
  /// transmitted by this call**; otherwise the outcome of the last attempt. How long to wait is the
  /// caller's decision: the specification repeats "until one is accepted" and fixes no limit.
  [[nodiscard]] pigeon::core::LinkStatus start_up(std::chrono::milliseconds max_wait);

  [[nodiscard]] pigeon::core::LinkStatus send_aiming_command(
      const pigeon::core::AimingCommand& command) override;
  [[nodiscard]] pigeon::core::LinkStatus send_fire_command(
      const pigeon::core::FireCommand& command) override;
  [[nodiscard]] pigeon::core::LinkStatus send_safe_state_command() override;

  /// Takes whatever has arrived off the line and acts on the valid messages in
  /// it, stamping each with the time it was taken. Never waits. `send_*` calls
  /// it while they wait for a reply; whoever owns the link calls it at least
  /// every 100 ms between commands, so that `health()` moves (section 7).
  ///
  /// If more than the stall bound has passed since it last ran, what the line
  /// buffered meanwhile is discarded unread: a backlog of old heartbeats must not
  /// make a stalled host look healthy.
  void poll() noexcept;

  /// Answered from state alone: transmits nothing and blocks on nothing
  /// (`REQ-COM-003`).
  [[nodiscard]] pigeon::core::LinkStatus health() const noexcept override;

 private:
  /// The one command whose reply is awaited (stop-and-wait, section 6).
  struct Pending {
    std::uint8_t type;
    std::uint8_t seq;
    std::optional<std::uint8_t> result;
  };

  /// Transmits `wire`, then waits for the matching reply or the reply timeout,
  /// and maps how the exchange ended to a status (section 6.1).
  [[nodiscard]] pigeon::core::LinkStatus exchange(pigeon::protocol::MessageType type,
                                                  std::uint8_t seq,
                                                  std::span<const std::uint8_t> wire);

  /// Waits for the line, but never longer than the reader period, so that the
  /// reader keeps running at the pace the stall rule assumes (section 7).
  void wait_for_line(std::chrono::milliseconds remaining) noexcept;

  /// The result of the reply that answered the outstanding command, if one has.
  [[nodiscard]] std::optional<std::uint8_t> pending_result() const noexcept;

  /// Whether a valid device message has been read in the last silence timeout.
  [[nodiscard]] bool heard_within_silence_timeout() const noexcept;

  void on_message(pigeon::protocol::ByteView encoded, std::chrono::milliseconds now) noexcept;

  ByteStream& line_;
  const pigeon::core::MonotonicClock& clock_;
  LinkTimings timings_;
  pigeon::protocol::MessageFramer framer_;
  std::optional<Pending> pending_;
  /// When the host last read a valid device message (section 7).
  std::optional<std::chrono::milliseconds> last_heard_;
  /// Set when an `AIM` or `FIRE` exchange ends rejected, cleared when one ends
  /// accepted. `SAFE_STATE` and faults leave it alone (section 7).
  bool rejected_{false};
  /// Set when an exchange ends in a fault and cleared when one is answered, even
  /// by a refusal. A heartbeat never clears it (section 7).
  bool faulted_{false};
  /// When `poll` last ran, for the stall rule.
  std::chrono::milliseconds last_poll_;
  std::uint8_t next_seq_{0};
};

}  // namespace pigeon::raspberry
