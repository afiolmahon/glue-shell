/**
 * Common helpers
 */
#ifndef CREW_UTIL_HPP
#define CREW_UTIL_HPP

#include <iostream>
#include <ranges>
#include <string>

namespace crew {

template <typename... T>
[[noreturn]] void fatal(T... args)
{
    std::cerr << "error: ";
    ((std::cerr << args), ...);
    std::cerr << "\n";
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

template <class R, class Value>
concept RangeOver = std::ranges::range<R>
        && std::same_as<std::ranges::range_value_t<R>, Value>;

} // namespace crew
#endif
