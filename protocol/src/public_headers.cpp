// Infrastructure, not behaviour.
//
// The codec is header-only, so without this translation unit nothing would
// compile its headers until a test did. It includes every public header of
// `pigeon::protocol` so that they are parsed by the compiler, checked by
// `-Werror` and analysed by clang-tidy from the moment they are written, as
// core/src/public_headers.cpp does for `core/`.
//
// It defines no behaviour and must never acquire any.

#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/cobs.hpp"
#include "pigeon/protocol/crc16.hpp"
#include "pigeon/protocol/message.hpp"
