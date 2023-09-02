
#include "../lib/include/util.hpp"
#include "../lib/include/interpreter.hpp"

#include <functional>
#include <sstream>
#include <iostream>
#include <map>

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include <sys/ioctl.h>

using crew::fatal;

template <typename... Args>
[[noreturn]] void die(fmt::format_string<Args...> format, Args... args)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    std::cerr << fmt::format(format, std::forward<Args>(args)...)
        << fmt::format(": {:s}", std::strerror(errno)) << std::endl;
    ::exit(1);
}

/** data */

struct Position {
    int x{};
    int y{};
};

struct EditorConfig {
    Position winSize{};
    struct termios origTermios;
};

static EditorConfig state;

/** Restore the terminal to its original state */
void exitRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.origTermios) == -1) {
        fatal("failed to tcsetattr: {}", std::strerror(errno));
    }
}

void enterRawMode()
{
    if (tcgetattr(STDIN_FILENO, &state.origTermios) == -1) {
        fatal("failed to tcgetattr: {}", std::strerror(errno));
    }

    struct termios raw = state.origTermios;
    // disable:
    // - translation of \r to \n (CarriageReturnNewLine)
    // - software control flow (ctrl-s, ctrl-q)
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // disable postprocessing (conversion of \n to \r\n)
    raw.c_oflag &= ~(OPOST);

    raw.c_cflag |= (CS8);

    // disable by clearing bitfields:
    // - echo (draw characters as they are input)
    // - canonical mode (read bits as they appear on stdin)
    // - signals (ctrl-z, ctrl-c)
    // - literal escape (ctrl-V, ctrl-O)
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // avoid blocking on read() by configuring a timeout
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        fatal("failed to tcsetattr: {}", std::strerror(errno));
    }

    // setup atexit handler to restore original terminal on exit
    ::atexit(exitRawMode);
}

int cookedRepl(crew::Vm& vm, std::ostream& out)
{
    out << "Repl:" << std::endl;
    out << "working dir is: " << std::filesystem::current_path() << std::endl;
    while (true) {
        out << ">";
        std::string in;
        getline(std::cin, in);
        auto tokens = crew::tokenize(in);
        if (auto parse = vm.parseTokens(tokens)) {
            out << *parse << "\n";
        } else {
            out << "NO COMMAND!\n";
        }
        out.flush();
    }
    return 0;
}

/** Convert char to control keycode i.e. 'q' -> CTRL-Q */
constexpr char ctrlKey(char k) { return k & 0x1F; }

/** terminal */

char readKey()
{
    int nread{};
    char c{};
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }
    return c;
}

std::optional<Position> getCursorPos()
{
    std::array<char, 32> buf;
    uint32_t i{};

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return {};
    }

    while (i < buf.size() - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        ++i;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') {
        return {};
    }

    Position pos{};
    if (sscanf(&buf[2], "%d;%d", &pos.y, &pos.x) != 2) {
        return {};
    }
    return pos;
}

std::optional<Position> getWindowSize()
{
    struct winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // fallback if ioctl basde lookup fails
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return {};
        }
        return getCursorPos();
    }
    return Position{ws.ws_col, ws.ws_row};
}

// TODO: on death, we should clear screen and reposition cursor

/** output */

void editorDrawRows()
{
    int y = 0;
    for (; y < state.winSize.y; ++y) {
        write(STDOUT_FILENO, "~", 1);

        if (y < state.winSize.y) {
            write(STDOUT_FILENO, "\r\n", 2);
        }

    }
}

void editorRefreshScreen()
{
    write(STDOUT_FILENO, "\x1b[?25l", 6); // hide cursor
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear entire screen
    write(STDOUT_FILENO, "\x1b[H", 3); // move cursor to top left
    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H", 3);
    write(STDOUT_FILENO, "\x1b[?25h", 6); // show cursor
}

/** input */

void processKeypress()
{
    char c = readKey();
    switch (c) {
    case ctrlKey('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        ::exit(0);
        break;
    }
}

/** init */

void initEditor() {
    auto ws = getWindowSize();
    if (!ws) {
        die("getWindowSize");
    }
    state.winSize = *ws;
}

int rawRepl()
{
    enterRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        processKeypress();
    }

    return 0;
}


int main(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    bool rawMode = true;
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (*it == "--raw") {
            rawMode = true;
        } else if (*it == "--cooked") {
            rawMode = false;
        }
    }

    crew::Vm vm{};
    vm.addParam("string", [](const std::string& s) { return !s.empty(); });
    vm.addParam("file", [](const std::string& s) { return std::filesystem::exists(s); });
    vm.addParam("directory", [](const std::string& s) { return std::filesystem::is_directory(s); });
    vm.addCommand("print", {"string"});
    vm.addCommand("print1", {"string"});
    vm.addCommand("print2", {"string", "string"});
    vm.addCommand("isfile", {"file"});
    vm.addCommand("isdir", {"directory"});


    if (rawMode) {
        return rawRepl();
    } else {
        return cookedRepl(vm, std::cout);
    }
    return 1;
}
