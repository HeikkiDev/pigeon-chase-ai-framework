# Thin wrapper around scripts/check.sh so that `make check` is always correct.
# scripts/check.sh remains the single source of truth.

.DEFAULT_GOAL := check
.PHONY: check build test format tidy fix fast clean help

## check: full gate - configure, build, test, format, clang-tidy
check:
	@scripts/check.sh

## fast: build and test only (inner development loop)
fast:
	@scripts/check.sh --skip-format --skip-tidy

## build: configure and build only
build:
	@scripts/check.sh --skip-tests --skip-format --skip-tidy

## test: build and run the test suite
test:
	@scripts/check.sh --skip-format --skip-tidy

## fix: rewrite sources with clang-format
fix:
	@scripts/check.sh --fix --skip-tidy --skip-tests

## clean: remove all build output
clean:
	@rm -rf build
	@echo "removed build/"

## help: list available targets
help:
	@grep -E '^## ' $(MAKEFILE_LIST) | sed 's/^## /  /'
