/**
 * Common helpers
 */
#ifndef CREW_UTIL_HPP
#define CREW_UTIL_HPP

#include <iostream>
#include <string>

#include <fmt/format.h>

namespace crew {

template <typename... Args>
[[noreturn]] void fatal(fmt::format_string<Args...> format, Args... args)
{
    std::cerr << fmt::format(format, std::forward<Args>(args)...) << std::endl;
    ::exit(1);
}

template <typename... Args>
[[noreturn]] void fatalErrno(fmt::format_string<Args...> format, Args... args)
{
    std::cerr << fmt::format(format, std::forward<Args>(args)...)
        << fmt::format(": {:s}", std::strerror(errno)) << std::endl;
    ::exit(1);
}

/** trim from both ends of string (right then left) */
inline std::string trim(std::string&& s)
{
    constexpr const char* const ws = " \t\n\r\f\v";
    s.erase(s.find_last_not_of(ws) + 1); // trim from end of string (right)
    s.erase(0, s.find_first_not_of(ws)); // trim from beginning of string (left)
    return std::move(s);
}
} // namespace crew
#endif
