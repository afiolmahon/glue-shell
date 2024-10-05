/**
 * Raw terminal mode input/output helpers
 */
#ifndef CREW_TERMINAL_TERMINAL_HPP
#define CREW_TERMINAL_TERMINAL_HPP

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include <fmt/format.h>

namespace crew {

template <typename... Args>
[[noreturn]] void die(fmt::format_string<Args...> format, Args... args)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    std::cerr << fmt::format(format, std::forward<Args>(args)...)
              << fmt::format(": {:s}", std::strerror(errno)) << std::endl;
    ::exit(1);
}

/** Convert char to control keycode i.e. 'q' -> CTRL-Q */
constexpr char ctrlKey(char k)
{
    return k & 0x1F;
}

/** Split a string into rows for rendering to the terminal */
std::vector<std::string> toRows(const std::string content, int32_t width);

struct Position {
    int x{};
    int y{};

    auto operator<=>(const Position& other) const = default;
};

/**
 * Escape sequences are converted to EditorKey values by readKey,
 */
enum class EditorKey : int {
    Backspace = 127,
    ArrowLeft = 1000,
    ArrowRight,
    ArrowUp,
    ArrowDown,
    DeleteKey,
    HomeKey,
    EndKey,
    PageUp,
    PageDown,
};

/** Read a key from standard input, stdin must be raw */
int readKey();

/** Get current cursor position, stdin must be raw */
std::optional<Position> getCursorPos();

/** Poll the current terminal window size, stdin must be raw */
std::optional<Position> getWindowSize();

} // namespace crew
#endif
