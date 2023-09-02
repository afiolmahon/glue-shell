
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
    str << fmt::format("[{:s}]{}", v.commandName, (v.command != nullptr ? "CMD" : "?"));

    // we need to merge indices in command and arg, print all that exist
    for (int i = 0; i < v.numArgs(); ++i) {
        const bool haveArgString = i < v.args.size();
        bool haveArgType = v.command != nullptr ? i < v.command->numParams()
                                                : false;
        if (v.command != nullptr) {
            haveArgType = i < v.command->numParams();
        }

        bool isValid = haveArgString && haveArgType && v.command->param(i).validate(v.args.at(i));

        str << fmt::format(" [{:s}]: {:s}",
                haveArgString ? v.args.at(i) : "?",
                haveArgType ? v.command->param(i).type : "?");

        // if we have both, validate the arg
        if (haveArgString && haveArgType) {
            str << "<" << (v.command->param(i).validate(v.args.at(i)) ? "Valid" : "Invalid") << ">";
        }
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
        m_params[id] = std::make_unique<VmParam>(id, validator);
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

int main(int argc, char** argv)
{
    auto& outStr = std::cout;

    outStr << "Repl:" << std::endl;
    outStr << "working dir is: " << std::filesystem::current_path() << std::endl;

    Vm vm;
    vm.addParam("string", [](const std::string& s) { return !s.empty(); });
    vm.addParam("file", [](const std::string& s) { return std::filesystem::exists(s); });
    vm.addParam("directory", [](const std::string& s) { return std::filesystem::is_directory(s); });
    vm.addCommand("print", {"string"});
    vm.addCommand("print1", {"string"});
    vm.addCommand("print2", {"string", "string"});
    vm.addCommand("isfile", {"file"});
    vm.addCommand("isdir", {"directory"});

    while (true) {
        outStr << ">";
        std::string in;
        getline(std::cin, in);
        auto tokens = tokenize(in);
        if (auto parse = vm.parseTokens(tokens)) {
            outStr << *parse << "\n";
        } else {
            outStr << "NO COMMAND!\n";
        }
        outStr.flush();
    }
}
