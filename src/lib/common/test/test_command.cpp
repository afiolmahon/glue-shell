#include <common/command.hpp>

#include <gtest/gtest.h>

using testing::ExitedWithCode;

namespace crew {
TEST(Command, RunExecPty)
{
    EXPECT_EXIT(Command("bash", "-c", "echo hello 1>&2")
                        .run(RunMode::ExecPty),
            ExitedWithCode(0),
            "hello");
}

TEST(Command, RunBlock)
{
    std::stringstream outStr;
    std::stringstream errStr;
    auto cmd = Command("bash", "-c", "echo 'helloErr' 1>&2; echo 'helloOut'")
                       .setOut(outStr)
                       .setErr(errStr);
    EXPECT_EQ(cmd.run(RunMode::Block), 0);
    EXPECT_EQ(outStr.str(), "helloOut\n");
    EXPECT_EQ(errStr.str(), "helloErr\n");
}

TEST(Command, RunBlockPty)
{
    std::stringstream outStr;
    std::stringstream errStr;
    auto cmd = Command("bash", "-c", "echo 'helloErr' 1>&2; echo 'helloOut'")
                       .setOut(outStr)
                       .setErr(errStr);
    EXPECT_EQ(cmd.run(RunMode::BlockPty), 0);
    EXPECT_EQ(outStr.str(), "helloErr\r\nhelloOut\r\n");
    EXPECT_EQ(errStr.str(), "");
}
} // namespace crew
