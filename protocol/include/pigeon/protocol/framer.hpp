#pragma once

#include <cstddef>
#include <cstdint>

#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/message.hpp"

namespace pigeon::protocol {

/// What feeding one byte to a `MessageFramer` produced.
enum class FramerEvent : std::uint8_t {
  /// The byte was absorbed; no message ended.
  NONE,
  /// A delimiter ended a message. `message()` holds its encoded bytes, which
  /// `receive_message` interprets.
  MESSAGE,
  /// A delimiter with nothing before it: an empty message, ignored.
  EMPTY,
  /// A delimiter ended a message that was over the receive bound. It was
  /// discarded whole, and nothing of it was kept or parsed.
  OVERSIZE,
};

/// Finds the messages in a stream of bytes, one byte at a time
/// (`device-protocol.md`, sections 3 and 3.4).
///
/// A message ends at the delimiter. The framer holds at most
/// `max_encoded_message_bytes` before it, in a buffer of that size and no
/// larger: more than that is an overflow, and after an overflow it discards
/// **everything up to and including the next delimiter**, parsing nothing of
/// what it threw away. The tail of an over-long message therefore cannot be read
/// as the start of a new one, however it was formed.
///
/// Every delimiter produces exactly one event other than `NONE`, and no other
/// byte produces one. The line may drop, corrupt or duplicate bytes; the framer
/// finds its feet again at the next delimiter, and whether what it found is a
/// good message is the receiver's question, answered by the CRC.
///
/// It does not allocate, throws nothing, and reads no clock. Time belongs to the
/// caller, who decides when to `reset()` it (a reconnection, a stalled reader).
class MessageFramer {
 public:
  constexpr MessageFramer() noexcept = default;

  /// Feeds one byte from the line.
  [[nodiscard]] constexpr FramerEvent push(std::uint8_t byte) noexcept {
    if (ready_) {
      reset();  // the previous message has been taken; begin the next
    }

    if (byte != delimiter) {
      if (!overflowed_ && buffer_.writable().set(length_, byte)) {
        ++length_;
      } else {
        overflowed_ = true;
      }
      return FramerEvent::NONE;
    }

    if (overflowed_) {
      reset();
      return FramerEvent::OVERSIZE;
    }
    if (length_ == 0) {
      return FramerEvent::EMPTY;
    }
    ready_ = true;
    return FramerEvent::MESSAGE;
  }

  /// The message that the last `push` completed, when it returned `MESSAGE`.
  /// Empty at every other time: a half-received message is never exposed. The
  /// view is valid until the next `push` or `reset`.
  [[nodiscard]] constexpr ByteView message() const noexcept {
    return ready_ ? buffer_.view(length_) : ByteView{};
  }

  /// Drops whatever has been received so far, including an overflow in
  /// progress, so that the next byte begins a new message.
  constexpr void reset() noexcept {
    length_ = 0;
    overflowed_ = false;
    ready_ = false;
  }

 private:
  FixedBuffer<max_encoded_message_bytes> buffer_;
  std::size_t length_{0};
  bool overflowed_{false};
  bool ready_{false};
};

}  // namespace pigeon::protocol
