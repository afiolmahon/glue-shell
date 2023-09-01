
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

struct Command {
    std::vector<Param> posParams;
};

struct ParseResult {
    std::string commandName{};
    bool isCommandValid{};
    size_t numArgs{};
    std::map<int, std::string> arg{};
    std::map<int, Param> argType{};
};

std::ostream& operator<<(std::ostream& str, const ParseResult& v)
{
    if (v.commandName.empty()) {
        str << "No command";
        return str;
    }

    // print command line
    str << "[" << v.commandName << "]" << (v.isCommandValid ? "CMD" : "?");

    // we need to merge indices in command and arg, print all that exist
    for (int i = 0; i < v.numArgs; ++i) {
        str << " ";
        auto argIt = v.arg.find(i);
        if (argIt != v.arg.end()) {
            str << "[" << argIt->second << "]";
        } else {
            str << "(?):";
        }
        auto annIt = v.argType.find(i);
        if (annIt != v.argType.end()) {
            str << annIt->second.type;
        } else {
            str << "?";
        }

        // if we have both, validate the arg
        if (argIt != v.arg.end() && annIt != v.argType.end()) {
            str << "<" << (annIt->second.validate(argIt->second) ? "Valid" : "Invalid") << ">";
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

        // determine if we have a valid command
        auto commandDefIt = commands.find(result.commandName);

        // populate map of supplied args - 0 is first arg rather than command
        for (int32_t i = 1; i < tokens.size(); ++i) {
            result.arg[i - 1] = tokens[i];
        }

        result.numArgs = tokens.size() - 1; // remove command from list
        if (commandDefIt != commands.end()) {
            result.isCommandValid = true;
            // upate numargs to handle if command expects more than supplied
            result.numArgs = std::max(result.numArgs, commandDefIt->second.posParams.size());
            // move annotations into map
            const auto& params = commandDefIt->second.posParams;
            for (int32_t i = 0; i < params.size(); ++i) {
                result.argType[i] = params.at(i);
            }
        }

        return result;
    }

    void addParam(const std::string& id, std::function<bool(const std::string&)> validator)
    {
        paramTypes[id] = Param{.type = id, .validate = validator};
    }

    void addCommand(const std::string& id, const std::vector<std::string>& paramIds)
    {
        std::vector<Param> params;
        for (const auto& p : paramIds) {
            // report error if param type doesn't exist
            if (auto it = paramTypes.find(p); it != paramTypes.end()) {
                params.push_back(it->second);
                continue;
            }
            fatal(fmt::format("addCommand({}, {}) failed; invalid param id '{}'\n",
                    id,
                    fmt::join(paramIds, ", "),
                    p));
        }
        commands.try_emplace(id, std::move(params));
    }

    /** Defines all parameters */
    std::map<std::string, Param> paramTypes{};
    std::map<std::string, Command> commands{};
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
