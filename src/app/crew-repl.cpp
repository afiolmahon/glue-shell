
#include "../lib/include/util.hpp"

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

struct VmParam {
    std::string type;
    std::function<bool(const std::string&)> validate;
};

class VmCommand {
public:
    size_t numParams() const { return m_posParams.size(); }
    const VmParam& param(size_t argPos) const
    {
        auto ptr = m_posParams.at(argPos);
        if (ptr == nullptr) {
            fatal("no param at {:d}", argPos);
        }
        return *ptr;
    }

    VmCommand(std::vector<const VmParam*> params) :
        m_posParams(std::move(params)) {}

private:
    std::vector<const VmParam*> m_posParams;
};

struct VmResult {
    std::string commandName{};
    const VmCommand* command = nullptr; // nullptr if no matching command exists
    std::vector<std::string> args;

    size_t numArgs() const
    {
        size_t result = args.size();
        if (command != nullptr) {
            result = std::max(result, command->numParams());
        }
        return result;
    }
};

std::ostream& operator<<(std::ostream& str, const VmResult& v)
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

class Vm {
public:
    std::optional<VmResult> parseTokens(std::vector<std::string> tokens)
    {
        if (tokens.empty()) {
            return {};
        }

        VmResult result{};
        result.commandName = tokens.front();
        result.command = findCommandPtr(result.commandName);
        result.args.assign(next(tokens.begin()), tokens.end());

        return result;
    }

    void addParam(const std::string& id, std::function<bool(const std::string&)> validator)
    {
        m_params[id] = std::make_unique<VmParam>(VmParam{id, validator});
    }

    void addCommand(const std::string& id, const std::vector<std::string>& paramIds)
    {
        std::vector<const VmParam*> params{};
        for (const auto& p : paramIds) {
            params.push_back(&getParam(p));
        }
        m_commands.try_emplace(id, std::make_unique<VmCommand>(std::move(params)));
    }

    /** get a stable pointer to a param definition */
    const VmParam& getParam(const std::string& id)
    {
        if (auto it = m_params.find(id); it != m_params.end()) {
            return *it->second;
        }
        fatal("invalid param id {:s}", id);
    }

    /** get a stable pointer to a command definition, or nullptr if it doesnt exist */
    const VmCommand* findCommandPtr(const std::string& name)
    {
        if (auto it = m_commands.find(name); it != m_commands.end()) {
            return &(*it->second);
        }
        return nullptr;
    }

    /** Defines all parameters */
private:
    std::map<std::string, std::unique_ptr<const VmParam>> m_params{};
    std::map<std::string, std::unique_ptr<const VmCommand>> m_commands{};
};

/** data */

struct EditorConfig {
    /** cols, rows (x, y) */
    std::pair<int, int> winSize{};
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

std::optional<std::pair<int, int>> getWindowSize()
{
    // TODO: fallback method for systems where ioctl won't work
    struct winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        return std::nullopt;
    }
    return std::make_pair<int, int>(ws.ws_col, ws.ws_row);
}

// TODO: on death, we should clear screen and reposition cursor

/** output */

void editorDrawRows()
{
    int y = 0;
    for (; y < state.winSize.second; ++y) {
        write(STDOUT_FILENO, "~", 1);

        if (y < state.winSize.second) {
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

int rawRepl(Vm& vm, std::ostream& out)
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

    Vm vm{};
    vm.addParam("string", [](const std::string& s) { return !s.empty(); });
    vm.addParam("file", [](const std::string& s) { return std::filesystem::exists(s); });
    vm.addParam("directory", [](const std::string& s) { return std::filesystem::is_directory(s); });
    vm.addCommand("print", {"string"});
    vm.addCommand("print1", {"string"});
    vm.addCommand("print2", {"string", "string"});
    vm.addCommand("isfile", {"file"});
    vm.addCommand("isdir", {"directory"});


    if (rawMode) {
        return rawRepl(vm, std::cout);
    } else {
        return cookedRepl(vm, std::cout);
    }
    return 1;
}
