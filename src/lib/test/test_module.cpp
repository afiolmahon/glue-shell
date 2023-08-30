
#include "../include/module.hpp"

#include <gtest/gtest.h>

using testing::ExitedWithCode;

namespace crew {

TEST(Module, ShModule)
{
    // XXX: no hardcoded path
    ModuleLoader loader("/home/antonio/src/crew/data");
    auto instance = loader.load("cmake");

    std::stringstream err;
    instance.command().setErr(err);
    EXPECT_EXIT(instance.command().run(RunMode::BlockPty),
            ExitedWithCode(1),
            "");
    EXPECT_EQ(err.str(), "hello");
}

} // namespace crew
