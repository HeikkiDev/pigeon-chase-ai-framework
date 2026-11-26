// Infrastructure, not behaviour.
//
// A header that no translation unit includes is never compiled, so a mistake
// in it is invisible until someone writes the first test against it. This
// translation unit includes every public header of `pigeon::raspberry` so that
// the interfaces are parsed by the compiler, checked by `-Werror` and analysed
// by clang-tidy from the moment they are written. It mirrors
// `core/src/public_headers.cpp`.
//
// It defines no behaviour and must never acquire any. Implementations belong
// in their own translation units, next to this one.

#include "pigeon/raspberry/ncnn_detector.hpp"
