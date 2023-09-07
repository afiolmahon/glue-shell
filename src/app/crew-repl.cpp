
#include "../lib/include/util.hpp"
#include "../lib/include/interpreter.hpp"

#include <functional>
#include <iostream>
#include <map>
#include <sstream>

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

struct Position {
    int x{};
    int y{};
};

/** Convert char to control keycode i.e. 'q' -> CTRL-Q */
constexpr char ctrlKey(char k)
{
    return k & 0x1F;
}

/** terminal */
int readKey()
{
    int nread{};
    char c{};
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }

    if (c == '\x1b') {
        std::array<char, 3> seq{};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '1':
                        return fmt::underlying(EditorKey::HomeKey);
                    case '3':
                        return fmt::underlying(EditorKey::DeleteKey);
                    case '4':
                        return fmt::underlying(EditorKey::EndKey);
                    case '5':
                        return fmt::underlying(EditorKey::PageUp);
                    case '6':
                        return fmt::underlying(EditorKey::PageDown);
                    case '7':
                        return fmt::underlying(EditorKey::HomeKey);
                    case '8':
                        return fmt::underlying(EditorKey::EndKey);
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A':
                    return fmt::underlying(EditorKey::ArrowUp);
                case 'B':
                    return fmt::underlying(EditorKey::ArrowDown);
                case 'C':
                    return fmt::underlying(EditorKey::ArrowRight);
                case 'D':
                    return fmt::underlying(EditorKey::ArrowLeft);
                case 'H':
                    return fmt::underlying(EditorKey::HomeKey);
                case 'F':
                    return fmt::underlying(EditorKey::EndKey);
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H':
                return fmt::underlying(EditorKey::HomeKey);
            case 'F':
                return fmt::underlying(EditorKey::EndKey);
            }
        }
        return '\x1b';
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
    struct winsize ws {};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // fallback if ioctl basde lookup fails
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return {};
        }
        return getCursorPos();
    }
    return Position{ws.ws_col, ws.ws_row};
}

struct Editor {

    Editor()
    {
        auto ws = getWindowSize();
        if (!ws) {
            die("getWindowSize");
        }
        winSize = *ws;
    }

    Position winSize{};
    Position cursor{}; // origin is 1,1, so must be offest when comparing to winsize

    std::string currentCommand;

    // this implementation will eventually need to support:
    // - unfinished jobs
    // - very large outputs
    struct OutputObject {
        std::string content{};

        // used for rendering, invalidated by resize
        mutable std::vector<std::string> rendered{};
        mutable bool dirty = true;

        void update(const Position& windowSize) const
        {
            rendered.clear();

            std::string next;
            int32_t rowWidth = 0;

            const auto pushRow = [this, &next, &rowWidth]() {
                rendered.push_back(std::move(next));
                next = {};
                rowWidth = 0;
            };

            auto it = content.begin();
            for (; it != content.end(); ++it) {
                // wrap if we reach the window length
                if (rowWidth == windowSize.x) {
                    pushRow();
                }
                if (*it == '\t') {
                    if (rowWidth + 4 >= windowSize.x) {
                        pushRow();
                    }
                    next.append("    "); // TODO: avoid translation to spaces once we couple tabwidth to that of the terminal
                    rowWidth += 4;
                } else if (*it == '\n') {
                    pushRow();
                } else {
                    next.push_back(*it);
                    rowWidth += 1;
                }
            }

            if (!next.empty()) {
                rendered.push_back(std::move(next));
            }
            dirty = false;
        }
    };

    struct Outputs {
        std::vector<OutputObject> entries;

        void render(std::string& buffer, int32_t numLines) const
        {
            // iterate backwards over each entry, line
            int32_t linesRendered = 0;
            auto it = entries.begin();
            while (it != entries.end() && linesRendered != numLines) {
                auto lit = it->rendered.begin();
                while (lit != it->rendered.end() && linesRendered != numLines) {
                    buffer.append(*lit);
                    buffer.append("\x1b[K\r\n"); // clear the current line, newline+cr
                    ++linesRendered;
                    ++lit;
                }
                ++it;
            };

            for (; linesRendered < numLines; ++linesRendered) {
                buffer.append("~ " + std::to_string(linesRendered)); // clear the current line, newline+cr
                buffer.append("\x1b[K\r\n"); // clear the current line, newline+cr
            }
        }

        // render method that takes # rows
    } outputs;

    void addOutput(std::string contents)
    {
        outputs.entries.push_back({.content = std::move(contents)});
        // TODO: move this to within drawRows, so that it can respond to window resize
        if (outputs.entries.back().dirty) {
            outputs.entries.back().update(winSize);
        }
    }

    /** input */
    void moveCursor(int key)
    {
        switch (key) {
        case fmt::underlying(EditorKey::ArrowLeft):
            if (cursor.x > 0) {
                --cursor.x;
            }
            break;
        case fmt::underlying(EditorKey::ArrowRight):
            if (cursor.x < winSize.x - 1) {
                ++cursor.x;
            }
            break;
        case fmt::underlying(EditorKey::ArrowUp):
            if (cursor.y > 0) {
                --cursor.y;
            }
            break;
        case fmt::underlying(EditorKey::ArrowDown):
            if (cursor.y < winSize.y - 1) {
                ++cursor.y;
            }
            break;
        }
    }

    /** input */
    void processKeypress()
    {
        int c = readKey();
        switch (c) {
        case '\r':
            addOutput(currentCommand);
            currentCommand = {};
            break;
        case ctrlKey('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            ::exit(0);
            break;
        case fmt::underlying(EditorKey::PageUp):
        case fmt::underlying(EditorKey::PageDown): {
            int times = winSize.y;
            while (times--) {
                moveCursor(c == fmt::underlying(EditorKey::PageUp)
                                ? fmt::underlying(EditorKey::ArrowUp)
                                : fmt::underlying(EditorKey::ArrowDown));
            }

        } break;
        case fmt::underlying(EditorKey::ArrowLeft):
        case fmt::underlying(EditorKey::ArrowRight):
        case fmt::underlying(EditorKey::ArrowUp):
        case fmt::underlying(EditorKey::ArrowDown):
            moveCursor(c);
            break;
        case fmt::underlying(EditorKey::HomeKey):
            cursor.x = 0;
            break;
        case fmt::underlying(EditorKey::EndKey):
            cursor.x = winSize.x - 1;
            break;
        case fmt::underlying(EditorKey::Backspace):
        case ctrlKey('h'):
            if (!currentCommand.empty()) {
                currentCommand.resize(currentCommand.size() - 1);
                cursor.x -= 1;
            }
            break;
        case ctrlKey('c'): // clear current
            currentCommand.clear();
            cursor.x = 1;
            break;
        case fmt::underlying(EditorKey::DeleteKey):
            // TODO:
            break;
        case ctrlKey('l'):
        case '\x1b': // ESC should have been translated by readKey()
            break;
        default:
            currentCommand.push_back(c);
            cursor.x += 1;
            break;
        }
    }

    /** output */
    void drawRows(std::string& buffer) const
    {
        int32_t promptLines = 2;
        int32_t terminalRows = winSize.y - promptLines;

        const auto clearCurrent = [&buffer](bool lastLine = false) {
            buffer.append("\x1b[K"); // clear the current line
            if (!lastLine) {
                buffer.append("\r\n");
            }
        };

        /** Append a string to the row, truncating to the window width*/
        const auto appendTruncated = [this, &buffer](const std::string& content) {
            int rowLen = std::min(
                    static_cast<int32_t>(content.size()), winSize.x);
            buffer.append(content.begin(), content.begin() + rowLen);
        };

        outputs.render(buffer, terminalRows);

        { // print current command
            appendTruncated(currentCommand);
            clearCurrent();
        }
        { // provide detail below
            buffer.append("crew interpreter - ctrl-q to quit");
            clearCurrent(true);
        }
    }
    void refreshScreen() const
    {
        std::string buffer;
        buffer.append("\x1b[?25l"); // hide cursor
        buffer.append("\x1b[H"); // move cursor to top left

        drawRows(buffer);

        std::array<char, 32> buf;
        snprintf(buf.data(), sizeof(buf), "\x1b[%d;%dH", cursor.y + 1, cursor.x + 1);
        buffer.append(buf.begin(), buf.begin() + strlen(buf.data()));

        buffer.append("\x1b[?25h"); // show cursor

        write(STDOUT_FILENO, buffer.c_str(), buffer.length());
    }
};

struct TerminalConfig {
    struct termios origTermios {};
};

static TerminalConfig s_terminalConfig;

/** Restore the terminal to its original state */
void exitRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_terminalConfig.origTermios) == -1) {
        fatal("failed to tcsetattr: {}", std::strerror(errno));
    }
}

void enterRawMode()
{
    if (tcgetattr(STDIN_FILENO, &s_terminalConfig.origTermios) == -1) {
        fatal("failed to tcgetattr: {}", std::strerror(errno));
    }

    struct termios raw = s_terminalConfig.origTermios;
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

int rawRepl()
{
    enterRawMode();
    Editor editor{};

    while (1) {
        editor.refreshScreen();
        editor.processKeypress();
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
