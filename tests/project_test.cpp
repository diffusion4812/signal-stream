#include <gtest/gtest.h>
#include "project.h"

TEST(HelloTest, BasicAssertions) {
    Project p;
    std::string err;
    bool result = LoadProjectFromFile("C:/Users/LOAR02/Downloads/test_project.json", p, err);

    EXPECT_EQ(p.name, "Test Project");
    EXPECT_EQ(p.streams.size(), 1);
}