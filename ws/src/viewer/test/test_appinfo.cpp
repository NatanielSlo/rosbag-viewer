#include <gtest/gtest.h>

#include "appinfo.h"

TEST(AppInfo, HasNameAndVersion) {
    EXPECT_FALSE(viewer::applicationName().isEmpty());
    EXPECT_FALSE(viewer::applicationVersion().isEmpty());
}
