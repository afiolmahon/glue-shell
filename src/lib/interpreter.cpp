#include "include/interpreter.hpp"

namespace crew {

std::ostream& operator<<(std::ostream& str, const ParseResult& v)
{
    str << fmt::format("{:s}",
            fmt::styled(v.commandName,
                    fmt::fg(v.command != nullptr ? fmt::color::green : fmt::color::red)));

    // we need to merge indices in command and arg, print all that exist
    for (int i = 0; i < v.numArgs(); ++i) {
        const bool haveArgString = i < v.args.size();
        bool haveArgType = v.command != nullptr ? i < v.command->numParams()
                                                : false;
        if (v.command != nullptr) {
            haveArgType = i < v.command->numParams();
        }

        bool isValid = haveArgString && haveArgType && v.command->param(i).validate(v.args.at(i));

        str << fmt::format(" {:s}({:s})",
                haveArgString ? v.args.at(i) : "?",
                fmt::styled(haveArgType ? v.command->param(i).type : "unknown",
                        fmt::fg(isValid ? fmt::color::green : fmt::color::red)));
    }
    return str;
}
} // namespace crew
