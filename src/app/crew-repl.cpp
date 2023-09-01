
#include "../lib/include/util.hpp"

#include <functional>
#include <iostream>
#include <map>

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

struct Param {
    std::string type;
    std::function<bool(const std::string&)> validate;
};

class Command {
public:
    size_t numParams() const { return m_posParams.size(); }
    const Param& param(size_t argPos) const
    {
        auto ptr = m_posParams.at(argPos);
        if (ptr == nullptr) {
            fatal(fmt::format("no param at {:d}", argPos));
        }
        return *ptr;
    }

    Command(std::vector<const Param*> params) :
        m_posParams(std::move(params)) {}

private:
    std::vector<const Param*> m_posParams;
};

struct ParseResult {
    std::string commandName{};
    const Command* command = nullptr; // nullptr if no matching command exists
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

std::ostream& operator<<(std::ostream& str, const ParseResult& v)
{
    if (v.commandName.empty()) {
        str << "No command";
        return str;
    }

    // print command line
    str << "[" << v.commandName << "]" << (v.command != nullptr ? "CMD" : "?");

    // we need to merge indices in command and arg, print all that exist
    for (int i = 0; i < v.numArgs(); ++i) {
        str << " ";

        bool haveArgString = i < v.args.size();
        bool haveArgType = false;
        if (v.command != nullptr) {
            haveArgType = i < v.command->numParams();
        }

        if (haveArgString) {
            str << "[" << v.args.at(i) << "]";
        } else {
            str << "(?):";
        }
        if (haveArgType) {
            str << v.command->param(i).type;
        } else {
            str << "?";
        }

        // if we have both, validate the arg
        if (haveArgString && haveArgType) {
            str << "<" << (v.command->param(i).validate(v.args.at(i)) ? "Valid" : "Invalid") << ">";
        }
    }
    return str;
}

struct Parser {
    std::optional<ParseResult> parseTokens(std::vector<std::string> tokens)
    {
        if (tokens.empty()) {
            return {};
        }

        ParseResult result{};
        result.commandName = tokens.front();
        result.command = findCommandPtr(result.commandName);
        result.args.assign(next(tokens.begin()), tokens.end());

        return result;
    }

    void addParam(const std::string& id, std::function<bool(const std::string&)> validator)
    {
        m_params[id] = std::make_unique<Param>(id, validator);
    }

    void addCommand(const std::string& id, const std::vector<std::string>& paramIds)
    {
        std::vector<const Param*> params{};
        for (const auto& p : paramIds) {
            params.push_back(&getParam(p));
        }
        m_commands.try_emplace(id, std::make_unique<Command>(std::move(params)));
    }

    /** get a stable pointer to a param definition */
    const Param& getParam(const std::string& id)
    {
        if (auto it = m_params.find(id); it != m_params.end()) {
            return *it->second;
        }
        fatal("invalid param id ", id);
    }

    /** get a stable pointer to a command definition, or nullptr if it doesnt exist */
    const Command* findCommandPtr(const std::string& name)
    {
        if (auto it = m_commands.find(name); it != m_commands.end()) {
            return &(*it->second);
        }
        return nullptr;
    }

    /** Defines all parameters */
private:
    std::map<std::string, std::unique_ptr<const Param>> m_params{};
    std::map<std::string, std::unique_ptr<const Command>> m_commands{};
};

int main(int argc, char** argv)
{
    auto& outStr = std::cout;

    outStr << "Repl:" << std::endl;
    outStr << "working dir is: " << std::filesystem::current_path() << std::endl;

    Parser p;
    p.addParam("string", [](const std::string& s) { return !s.empty(); });
    p.addParam("file", [](const std::string& s) { return std::filesystem::exists(s); });
    p.addParam("directory", [](const std::string& s) { return std::filesystem::is_directory(s); });
    p.addCommand("print1", {"string"});
    p.addCommand("print2", {"string", "string"});
    p.addCommand("isfile", {"file"});
    p.addCommand("isdir", {"directory"});

    while (true) {
        outStr << ">";
        std::string in;
        getline(std::cin, in);
        auto tokens = tokenize(in);
        if (auto parse = p.parseTokens(tokens)) {
            outStr << *parse << "\n";
        } else {
            outStr << "NO COMMAND!\n";
        }
        outStr.flush();
    }
}
