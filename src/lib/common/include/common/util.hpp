/**
 * Common helpers
 */
#ifndef CREW_UTIL_HPP
#define CREW_UTIL_HPP

#include <iostream>

#include <fmt/format.h>

namespace crew {

template <typename... Args>
[[noreturn]] void fatal(fmt::format_string<Args...> format, Args... args)
{
    std::cerr << fmt::format(format, std::forward<Args>(args)...) << std::endl;
    ::exit(1);
}

} // namespace crew
#endif
