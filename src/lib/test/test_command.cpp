#include "../include/command.hpp"

#include <gtest/gtest.h>

using testing::ExitedWithCode;

namespace crew {
TEST(Command, execPty)
{
    EXPECT_EXIT(Command("bash", "-c", "echo hello 1>&2")
                        .run(RunMode::ExecPty),
            ExitedWithCode(0),
            "hello");
}
} // namespace crew
