#!/usr/bin/env bash
#
# Verifies: REQ-DEV-001 (the project builds and checks from a clean checkout).
#
# clang-format and clang-tidy are pinned in scripts/lint-tools.txt (ADR-0022).
# They were unpinned (ADR-0017) until two machines disagreed: a gate that passed
# on a developer's clang-format 18 failed in CI on clang-format 23, and the same
# happened again with clang-tidy, because the formatters and linters of different
# major versions disagree about perfectly valid code.
#
# These tests run copies of check.sh against stub tools that report whatever
# version they are told to, so a mismatch can be made on any host. A mismatch is
# a warning off CI and a failure under PIGEON_STRICT_TOOLS=1, which CI sets.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIGEON_REPO_ROOT="${PIGEON_REPO_ROOT:-$(cd "$HERE/../.." && pwd)}"
export PIGEON_REPO_ROOT
# shellcheck source=tests/scripts/support.sh
source "$HERE/support.sh"

echo "check.sh (pinned tool versions)"

readonly PIN_FORMAT="23.1.5"
readonly PIN_TIDY="23.1.4"

# new_check_tree <clang-format version> <clang-tidy version> — a sandbox with a
# copy of check.sh, its own pin file, a git index with one source file, a compile
# database naming it, and stub tools that report the given versions.
new_check_tree() {
  local format_version="$1"
  local tidy_version="$2"
  local dir
  dir="$(new_sandbox)"

  mkdir -p "$dir/scripts" "$dir/bin" "$dir/build/macos-debug"
  cp "$PIGEON_REPO_ROOT/scripts/check.sh" "$dir/scripts/check.sh"
  printf 'clang-format==%s\nclang-tidy==%s\n' "$PIN_FORMAT" "$PIN_TIDY" \
    > "$dir/scripts/lint-tools.txt"

  local root
  root="$(cd "$dir" && pwd)"
  printf '[{"directory": "%s", "file": "%s/core/src/example.cpp"}]\n' \
    "$root" "$root" > "$dir/build/macos-debug/compile_commands.json"

  git -C "$dir" init -q
  git -C "$dir" add core

  write_file "$dir/bin/cmake" <<'EOF2'
#!/usr/bin/env bash
exit 0
EOF2
  write_file "$dir/bin/uname" <<'EOF2'
#!/usr/bin/env bash
echo Linux
EOF2
  write_file "$dir/bin/clang-format" <<EOF2
#!/usr/bin/env bash
if [[ "\${1:-}" == "--version" ]]; then
  echo "clang-format version $format_version"
fi
exit 0
EOF2
  write_file "$dir/bin/clang-tidy" <<EOF2
#!/usr/bin/env bash
if [[ "\${1:-}" == "--version" ]]; then
  echo "LLVM (http://llvm.org/):"
  echo "  LLVM version $tidy_version"
  echo "  Optimized build."
fi
exit 0
EOF2
  chmod +x "$dir/bin/"*

  printf '%s' "$dir"
}

# run_gate <sandbox> <strict: 0|1> — formatting and clang-tidy only.
run_gate() {
  local dir="$1"
  local strict="$2"
  PATH="$dir/bin:$PATH" PIGEON_STRICT_TOOLS="$strict" "$dir/scripts/check.sh" \
    --skip-tests --skip-arch --skip-trace --skip-workflow
}

# ------------------------------------------------------------ matching pin --
sandbox="$(new_check_tree "$PIN_FORMAT" "$PIN_TIDY")"
expect_exit 'passes when both tools are the pinned versions, strict' 0 \
  run_gate "$sandbox" 1

sandbox="$(new_check_tree "$PIN_FORMAT" "$PIN_TIDY")"
expect_output_not_contains 'says nothing about versions when they match' \
  'scripts/lint-tools.txt' run_gate "$sandbox" 1

# ------------------------------------------------------- clang-format differs --
sandbox="$(new_check_tree "18.1.3" "$PIN_TIDY")"
expect_exit 'fails under strict tools when clang-format is not the pinned version' 1 \
  run_gate "$sandbox" 1

sandbox="$(new_check_tree "18.1.3" "$PIN_TIDY")"
expect_exit 'only warns off strict tools when clang-format is not the pinned version' 0 \
  run_gate "$sandbox" 0

sandbox="$(new_check_tree "18.1.3" "$PIN_TIDY")"
expect_output_contains 'names the pinned clang-format version' \
  "$PIN_FORMAT" run_gate "$sandbox" 0

sandbox="$(new_check_tree "18.1.3" "$PIN_TIDY")"
expect_output_contains 'says how to install the pinned versions' \
  'pip install -r scripts/lint-tools.txt' run_gate "$sandbox" 0

# -------------------------------------------------------- clang-tidy differs --
sandbox="$(new_check_tree "$PIN_FORMAT" "18.1.3")"
expect_exit 'fails under strict tools when clang-tidy is not the pinned version' 1 \
  run_gate "$sandbox" 1

sandbox="$(new_check_tree "$PIN_FORMAT" "18.1.3")"
expect_exit 'only warns off strict tools when clang-tidy is not the pinned version' 0 \
  run_gate "$sandbox" 0

sandbox="$(new_check_tree "$PIN_FORMAT" "18.1.3")"
expect_output_contains 'names the pinned clang-tidy version' \
  "$PIN_TIDY" run_gate "$sandbox" 0

# ------------------------------------------------- the repository's real pin --
# The file CI installs from must name both tools, each at an exact version.
expect_exit 'the repository pins clang-format to an exact version' 0 \
  grep -Eq '^clang-format==[0-9]+\.[0-9]+\.[0-9]+$' "$PIGEON_REPO_ROOT/scripts/lint-tools.txt"
expect_exit 'the repository pins clang-tidy to an exact version' 0 \
  grep -Eq '^clang-tidy==[0-9]+\.[0-9]+\.[0-9]+$' "$PIGEON_REPO_ROOT/scripts/lint-tools.txt"

finish
