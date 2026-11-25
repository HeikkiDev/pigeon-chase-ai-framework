// Compiles every public header of pigeon::protocol in this target's own
// configuration: C++17, -fno-exceptions, -fno-rtti (ADR-0020). An
// interface-only change is therefore still checked by the compiler, under the
// rules the Arduino Uno imposes, and by clang-tidy.
#include "pigeon/protocol/protocol.hpp"
