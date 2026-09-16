// Infrastructure, not behaviour.
//
// A header that no translation unit includes is never compiled, so a mistake
// in it is invisible until someone writes the first test against it — which in
// this repository happens in a different agent's hands, in a different commit.
// This translation unit includes every public header of `pigeon::core` so that
// the interfaces are parsed by the compiler, checked by `-Werror` and analysed
// by clang-tidy from the moment they are written.
//
// It defines no behaviour and must never acquire any. Implementations belong in
// their own translation units, next to this one.

#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/aiming.hpp"
#include "pigeon/core/build_info.hpp"
#include "pigeon/core/clock.hpp"
#include "pigeon/core/configuration.hpp"
#include "pigeon/core/detection.hpp"
#include "pigeon/core/detector.hpp"
#include "pigeon/core/frame.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/core/safety_policy.hpp"
#include "pigeon/core/target_state.hpp"
#include "pigeon/core/track.hpp"
