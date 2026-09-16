// Verifies: build harness only (no product requirement).
//
// This test exists so that `scripts/check.sh` proves the configure, build,
// link and test-discovery pipeline end to end even before product code
// exists. Do not delete it when real tests are added.

#include "pigeon/core/build_info.hpp"

#include <gtest/gtest.h>

namespace {

TEST(BuildInfo, ReportsANonEmptyVersion) { EXPECT_FALSE(pigeon::core::version().empty()); }

}  // namespace
