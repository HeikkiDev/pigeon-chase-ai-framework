# Thin wrapper around scripts/check.sh so that `make check` is always correct.
# scripts/check.sh remains the single source of truth.

.DEFAULT_GOAL := check
.PHONY: check build test format tidy fix fast clean help arch trace workflow asan

## check: full gate - build, test, architecture, determinism, traceability, workflow, format, tidy
check:
	@scripts/check.sh

## fast: build and test only (inner development loop)
fast:
	@scripts/check.sh --skip-format --skip-tidy --skip-determinism --skip-workflow

## build: configure and build only
build:
	@scripts/check.sh --skip-tests --skip-format --skip-tidy --skip-arch \
		--skip-determinism --skip-trace --skip-workflow

## test: build and run the test suite
test:
	@scripts/check.sh --skip-format --skip-tidy --skip-determinism --skip-workflow

## asan: full gate under AddressSanitizer and UndefinedBehaviorSanitizer
asan:
	@scripts/check.sh --preset macos-asan --skip-format --skip-tidy

## arch: check that core/ is hardware-free and the suite needs no device
arch:
	@scripts/arch-check.sh
	@scripts/header-check.sh

## trace: print the requirement traceability matrix
trace:
	@scripts/trace.sh

## workflow: check red-before-green evidence in the commit history
workflow:
	@scripts/tdd-check.sh

## fix: rewrite sources with clang-format
fix:
	@scripts/check.sh --fix --skip-tidy --skip-tests --skip-arch \
		--skip-determinism --skip-trace --skip-workflow

## clean: remove all build output
clean:
	@rm -rf build
	@echo "removed build/"

## help: list available targets
help:
	@grep -E '^## ' $(MAKEFILE_LIST) | sed 's/^## /  /'
