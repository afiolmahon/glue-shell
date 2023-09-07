#ifndef CREW_INTERPRETER_HPP
#define CREW_INTERPRETER_HPP

#include "util.hpp"

#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/color.h>
#include <fmt/format.h>

namespace crew {

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

struct ParseResult {
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

std::ostream& operator<<(std::ostream& str, const ParseResult& v);

class Vm {
public:
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
} // namespace crew
#endif
