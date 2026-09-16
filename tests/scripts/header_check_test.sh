#!/usr/bin/env bash
#
# Verifies: REQ-DEV-001 (the project builds from a clean checkout with no
#           hardware present).
#
# A public header that only compiles because some *other* header was included
# first is a latent build failure. It is invisible for as long as every
# translation unit happens to include its accidental provider first, and it
# surfaces on the day somebody writes the one file that does not — usually the
# first implementation file for that very header.
#
# core/src/public_headers.cpp cannot catch this. It includes every public
# header together, so it proves the *set* compiles; self-containment is a
# property of each header *alone*. These acceptance criteria are therefore
# verified by running scripts/header-check.sh against trees that deliberately
# violate them.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIGEON_REPO_ROOT="${PIGEON_REPO_ROOT:-$(cd "$HERE/../.." && pwd)}"
export PIGEON_REPO_ROOT
# shellcheck source=tests/scripts/support.sh
source "$HERE/support.sh"

HEADER_CHECK="$PIGEON_REPO_ROOT/scripts/header-check.sh"

echo "header-check.sh"

# -------------------------------------------------------------- nominal case --
sandbox="$(new_sandbox)"
expect_exit 'accepts a tree whose headers are self-contained' 0 \
  "$HEADER_CHECK" --root "$sandbox"

# ------------------------------------------------- a header missing an include --
# The real defect this gate was written for: a defaulted three-way comparison
# needs <compare> for std::partial_ordering, and nothing says so at the point
# of use.
sandbox="$(new_sandbox)"
write_file "$sandbox/core/include/pigeon/core/geometry.hpp" <<'EOF'
#pragma once

#include <cstdint>

namespace pigeon::core {
struct PixelDistance {
  double value{0.0};
  [[nodiscard]] constexpr auto operator<=>(const PixelDistance&) const = default;
};
}  // namespace pigeon::core
EOF
expect_exit 'rejects a header that does not include what it uses' 1 \
  "$HEADER_CHECK" --root "$sandbox"

expect_output_contains 'names the header that is not self-contained' \
  'geometry.hpp' "$HEADER_CHECK" --root "$sandbox"

# --------------------------------------------------------- the ordering trap --
# This is the property public_headers.cpp cannot have, and the reason this gate
# exists as a separate check rather than one more #include in that file.
#
# Compiled together in alphabetical order, these two headers are clean: the
# first drags in <compare> and the second silently borrows it. Compiled alone,
# the second is broken. A gate that concatenates includes reports success here.
sandbox="$(new_sandbox)"
write_file "$sandbox/core/include/pigeon/core/aaa_provider.hpp" <<'EOF'
#pragma once

#include <compare>

namespace pigeon::core {
struct Provider {
  int value{0};
  [[nodiscard]] constexpr auto operator<=>(const Provider&) const = default;
};
}  // namespace pigeon::core
EOF
write_file "$sandbox/core/include/pigeon/core/zzz_borrower.hpp" <<'EOF'
#pragma once

namespace pigeon::core {
struct Borrower {
  int value{0};
  [[nodiscard]] constexpr auto operator<=>(const Borrower&) const = default;
};
}  // namespace pigeon::core
EOF
write_file "$sandbox/core/src/public_headers.cpp" <<'EOF'
#include "pigeon/core/aaa_provider.hpp"
#include "pigeon/core/zzz_borrower.hpp"
EOF
expect_exit 'rejects a header that only compiles after another header' 1 \
  "$HEADER_CHECK" --root "$sandbox"

expect_output_contains 'names the borrower, not the provider' \
  'zzz_borrower.hpp' "$HEADER_CHECK" --root "$sandbox"

expect_output_not_contains 'does not blame the header that was already correct' \
  'aaa_provider.hpp' "$HEADER_CHECK" --root "$sandbox"

# ------------------------------------------------------------- nothing to check --
# core/ need not contain any public header yet, and a gate that fails on an
# empty tree cannot be adopted before the code it polices exists.
sandbox="$(new_sandbox)"
rm -f "$sandbox/core/include/pigeon/core/example.hpp"
expect_exit 'permits a tree with no public headers' 0 \
  "$HEADER_CHECK" --root "$sandbox"

# ------------------------------------------------------------- scope of the check --
# Only core/ is policed. raspberry/ and arduino/ are hardware layers whose
# headers legitimately need a toolchain this gate does not have.
sandbox="$(new_sandbox)"
write_file "$sandbox/raspberry/camera.hpp" <<'EOF'
#pragma once

namespace pigeon::raspberry {
struct Camera {
  int fd{-1};
  [[nodiscard]] constexpr auto operator<=>(const Camera&) const = default;
};
}  // namespace pigeon::raspberry
EOF
expect_exit 'ignores headers outside core/' 0 \
  "$HEADER_CHECK" --root "$sandbox"

# ------------------------------------------------------------ the real repository --
expect_exit 'the real repository has only self-contained public headers' 0 \
  "$HEADER_CHECK" --root "$PIGEON_REPO_ROOT"

finish
