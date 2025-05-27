#include <cstdio>

#include <gtest/gtest.h>

#include <token.h>

TEST(TestEmptyToken, EmptyTokenInTypeNone) {
  Token token;
  EXPECT_EQ(token.get_type(), TOKEN_NONE);
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}