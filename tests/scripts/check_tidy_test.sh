#!/usr/bin/env bash
#
# Verifies: REQ-DEV-001 (the project builds from a clean checkout with no
#           hardware present).
#
# scripts/check.sh is the gate, so a defect in the gate itself fails every
# build regardless of the code. One such defect was found on Linux: off macOS
# the clang-tidy argument array is empty, and "${tidy_args[@]:-}" expanded to
# one empty-string argument, which clang-tidy rejects ("no checks enabled").
# macOS CI could never see it, because there the array holds the SDK path.
#
# These tests run the clang-tidy step of a copy of check.sh against stub tools,
# with `uname` stubbed too, so both platform branches are exercised on any
# host. Every other step is skipped, and `cmake` is a no-op stub.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIGEON_REPO_ROOT="${PIGEON_REPO_ROOT:-$(cd "$HERE/../.." && pwd)}"
export PIGEON_REPO_ROOT
# shellcheck source=tests/scripts/support.sh
source "$HERE/support.sh"

echo "check.sh (clang-tidy step)"

# new_check_tree <os-name> — prints a sandbox holding a copy of check.sh, a git
# index with one source file, a compile database naming it, and a bin/ of stub
# tools. The clang-tidy stub fails on any empty argument and records the rest.
new_check_tree() {
  local os="$1"
  local dir
  dir="$(new_sandbox)"

  mkdir -p "$dir/scripts" "$dir/bin" "$dir/build/macos-debug"
  cp "$PIGEON_REPO_ROOT/scripts/check.sh" "$dir/scripts/check.sh"

  # check.sh resolves REPO_ROOT through `pwd`, which may differ from $dir when
  # TMPDIR is a symlink (macOS), so the compile database uses the same form.
  local root
  root="$(cd "$dir" && pwd)"
  printf '[{"directory": "%s", "file": "%s/core/src/example.cpp"}]\n' \
    "$root" "$root" > "$dir/build/macos-debug/compile_commands.json"

  git -C "$dir" init -q
  git -C "$dir" add core

  write_file "$dir/bin/cmake" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
  write_file "$dir/bin/uname" <<EOF
#!/usr/bin/env bash
echo "$os"
EOF
  write_file "$dir/bin/xcrun" <<'EOF'
#!/usr/bin/env bash
echo "/Applications/Xcode SDKs/MacOSX.sdk"
EOF
  write_file "$dir/bin/clang-tidy" <<'EOF'
#!/usr/bin/env bash
if [[ "${1:-}" == "--version" ]]; then
  echo "LLVM version 0.0.0-stub"
  exit 0
fi
for arg in "$@"; do
  if [[ -z "$arg" ]]; then
    echo "stub clang-tidy: received an empty argument" >&2
    exit 1
  fi
done
printf '[%s]' "$@" > "$(dirname "$0")/../tidy-args"
exit 0
EOF
  chmod +x "$dir/bin/"*

  printf '%s' "$dir"
}

run_tidy_step() {
  local dir="$1"
  PATH="$dir/bin:$PATH" PIGEON_STRICT_TOOLS=1 "$dir/scripts/check.sh" \
    --skip-format --skip-tests --skip-arch --skip-trace --skip-workflow
}

# ------------------------------------------------- off macOS: no extra args --
sandbox="$(new_check_tree Linux)"
expect_exit 'runs clang-tidy without an empty argument off macOS' 0 \
  run_tidy_step "$sandbox"
# Exact match, so a stray extra argument fails as well as an empty one.
expect_exit 'passes exactly -p, --quiet and the sources off macOS' 0 \
  grep -qxF -- "[-p][build/macos-debug][--quiet][core/src/example.cpp]" \
  "$sandbox/tidy-args"

# ------------------------------------------ on macOS: the SDK path, intact --
sandbox="$(new_check_tree Darwin)"
expect_exit 'runs clang-tidy with the SDK arguments on macOS' 0 \
  run_tidy_step "$sandbox"
expect_exit 'passes the SDK path, spaces intact, as one argument on macOS' 0 \
  grep -qxF -- "[-p][build/macos-debug][--quiet][--extra-arg=-isysroot][--extra-arg=/Applications/Xcode SDKs/MacOSX.sdk][core/src/example.cpp]" \
  "$sandbox/tidy-args"

finish
