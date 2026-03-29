#include <gtest/gtest.h>

#include "util.h"

TEST(UtilTests, ReturnsConfiguredProjectVersion) {
  EXPECT_EQ(lgx::applicationVersionView(), std::string_view{LGX_VERSION});
  EXPECT_EQ(lgx::applicationVersion(), QString::fromLatin1(LGX_VERSION));
}
