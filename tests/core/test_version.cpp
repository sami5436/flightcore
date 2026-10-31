#include "fc/core/version.hpp"

#include <gtest/gtest.h>

TEST(Version, MatchesProjectVersion) {
  EXPECT_EQ(fc::core::version(), "0.1.0");
}
