
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

using crew::fatal;

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

/**
 * RAII raw terminal mode wrapper based on
 * https://viewsourcecode.org/snaptoken/kilo/02.enteringRawMode.html
 */
class RawTerminalRaii {
public:
    /** Puts terminal into raw input mode */
    RawTerminalRaii()
    {
        if (tcgetattr(STDIN_FILENO, &m_orig) == -1) {
            fatal("failed to tcgetattr: {}", std::strerror(errno));
        }

        struct termios raw = m_orig;
        // disable:
        // - translation of \r to \n (CarriageReturnNewLine)
        // - software control flow (ctrl-s, ctrl-q)
        raw.c_iflag &= ~(ICRNL | IXON);
        // disable postprocessing (conversion of \n to \r\n)
        raw.c_oflag &= ~(OPOST);
        // disable by clearing bitfields:
        // - echo (draw characters as they are input)
        // - canonical mode (read bits as they appear on stdin)
        // - signals (ctrl-z, ctrl-c)
        // - literal escape (ctrl-V, ctrl-O)
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        // disable canonical mode
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
            fatal("failed to tcsetattr: {}", std::strerror(errno));
        }
    }

    /** no copy */
    RawTerminalRaii(const RawTerminalRaii&) = delete;
    RawTerminalRaii& operator=(const RawTerminalRaii&) = delete;
    /** no move */
    RawTerminalRaii(RawTerminalRaii&&) = delete;
    RawTerminalRaii& operator=(RawTerminalRaii&&) = delete;

    /** Restore the terminal to its original state */
    // TODO: registering an atexit(*void()) handler
    // is likely more robust as abort() may circumvent RAII
    ~RawTerminalRaii()
    {
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &m_orig) == -1) {
            fatal("failed to tcsetattr: {}", std::strerror(errno));
        }
    }

private:
    struct termios m_orig {};
};

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

int rawRepl(Vm& vm, std::ostream& out)
{
    RawTerminalRaii rawMode{};

    // assemble the output
    while (true) {
        char c{};
        if (::read(STDIN_FILENO, &c, 1) == -1) {
            fatal("::read failed: {:s}", std::strerror(errno));
        }
        if (c == 3) { // handle ctrl-c
            out << "exiting...\r\n";
            return 0;
        }
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
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
