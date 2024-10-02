
#include <common/interpreter.hpp>
#include <common/util.hpp>
#include <terminal/terminal.hpp>

#include <functional>
#include <sstream>

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <stdio.h>
#include <termios.h>

namespace crew {

std::vector<std::string> tokenize(std::string in)
{
    std::vector<std::string> tokens;

    // stringstream class check1
    std::stringstream check1(in);

    std::string intermediate;

    // Tokenizing w.r.t. space ' '
    while (getline(check1, intermediate, ' ')) {
        tokens.push_back(intermediate);
    }
    return tokens;
}

class RenderableWrappedText {
public:
    RenderableWrappedText(std::string content) :
        m_content(std::move(content)) {}

    /** Get wrapped content, lazily regenerating if width changes */
    const std::vector<std::string> rows(int32_t cols) const
    {
        if (m_cols != cols) {
            m_rendered = toRows(m_content, cols);
            m_cols = cols;
        }
        return m_rendered;
    }

private:
    std::string m_content;

    // render state
    mutable std::optional<int32_t> m_cols; // validity of m_render
    mutable std::vector<std::string> m_rendered;
};

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

    struct Outputs {
        std::vector<RenderableWrappedText> entries;

        void render(std::string& buffer, int32_t numLines) const
        {
            // iterate backwards over each entry, line
            int32_t linesRendered = 0;
            auto it = entries.begin();
            while (it != entries.end() && linesRendered != numLines) {
                const std::vector<std::string>& rows = it->rows(numLines);
                auto lit = rows.begin();
                while (lit != rows.end() && linesRendered != numLines) {
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
            outputs.entries.emplace_back(std::move(currentCommand));
            currentCommand = {};
            cursor.x = 0;
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
    void drawRows(std::string& buffer)
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
            cursor.y = terminalRows;
            appendTruncated(currentCommand);
            clearCurrent();
        }
        { // provide detail below
            buffer.append("crew interpreter - ctrl-q to quit");
            clearCurrent(true);
        }
    }
    void refreshScreen()
    {
        std::string buffer;
        buffer.append("\x1b[?25l"); // hide cursor
        buffer.append("\x1b[H"); // move cursor to top left

        drawRows(buffer);

        { // move cursor to position specified by the `cursor` member
            std::array<char, 32> buf;
            snprintf(buf.data(), sizeof(buf), "\x1b[%d;%dH", cursor.y + 1, cursor.x + 1);
            buffer.append(buf.begin(), buf.begin() + strlen(buf.data()));
        }

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

int cookedRepl(Vm& vm, std::ostream& out)
{
    out << "Repl:" << std::endl;
    out << "working dir is: " << std::filesystem::current_path() << std::endl;
    while (true) {
        out << ">";
        std::string in;
        getline(std::cin, in);
        auto tokens = tokenize(in);
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
} // namespace crew

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
        return crew::rawRepl();
    } else {
        return cookedRepl(vm, std::cout);
    }
    return 1;
}
