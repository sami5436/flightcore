#include <gtest/gtest.h>

#include "fc/core/version.hpp"

TEST(Version, MatchesProjectVersion) { EXPECT_EQ(fc::core::version(), "0.1.0"); }
