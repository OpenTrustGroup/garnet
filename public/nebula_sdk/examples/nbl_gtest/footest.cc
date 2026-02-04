#include<gtest/gtest.h>

int Foo(int a, int b)
{
    int c = a % b;
    if(c == 0)
        return b;
    return Foo(b,c);
}

TEST(FooTest,HandleNoneZeroInput)
{
    EXPECT_EQ(2,Foo(4,10));
    EXPECT_EQ(6,Foo(30,18));
}

int main(int argc, char **argv) {
  printf("Running main() from %s\n", __FILE__);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}