/**
 * Wrapper for running an external commands and obtaining its output
 */
#ifndef CREW_COMMAND_HPP
#define CREW_COMMAND_HPP

#include "util.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace crew {

class Command {
public:
    explicit Command(std::string command) :
        m_command(std::move(command)) {}

    Command& arg(std::string arg) &
    {
        m_args.push_back(arg);
        return *this;
    }
    Command arg(std::string arg) &&
    {
        m_args.push_back(arg);
        return std::move(*this);
    }

    template <typename First, typename... Rest>
    Command args(First&& first, Rest&&... rest) &&
    {
        m_args.emplace_back(std::forward<First>(first));
        return args<Rest...>(std::forward<Rest>(rest)...);
    }
    template <typename First, typename... Rest>
    Command& args(First&& first, Rest&&... rest) &
    {
        m_args.emplace_back(std::forward<First>(first));
        return args<Rest...>(std::forward<Rest>(rest)...);
    }

    Command& setEnv(const std::string& k, std::string value) &
    {
        m_envOverride[k] = std::move(value);
        return *this;
    }
    Command setEnv(const std::string& k, std::string value) &&
    {
        m_envOverride[k] = std::move(value);
        return std::move(*this);
    }

    Command& setCurrentDir(std::optional<std::filesystem::path> directory) &
    {
        m_cd = std::move(directory);
        return *this;
    }
    Command setCurrentDir(std::optional<std::filesystem::path> directory) &&
    {
        m_cd = std::move(directory);
        return std::move(*this);
    }

    Command& setVerbose(bool verbose) &
    {
        m_verbose = verbose;
        return *this;
    }
    Command setVerbose(bool verbose) &&
    {
        m_verbose = verbose;
        return std::move(*this);
    }

    int run(std::ostream& outStr = std::cout, std::ostream& errStr = std::cerr) const;
    int runPty(std::ostream& outStr = std::cout) const;

    void describe(std::ostream& str = std::cerr) const
    {
        str << m_command << " ";
        for (const auto& arg : m_args) {
            str << arg << " ";
        }
        str << std::endl;
        if (m_cd.has_value()) {
            str << "\t- executing from directory: " << *m_cd << std::endl;
        }
        if (!m_envOverride.empty()) {
            str << "\t- overriding "
                << m_envOverride.size() << " environment variables" << std::endl;
        }
    }

private:
    // recursive base case for the args(T...) methods
    template <typename None = void>
    Command args() &&
    {
        return std::move(*this);
    }
    template <typename None = void>
    Command& args() &
    {
        return *this;
    }

    bool m_verbose{};
    std::string m_command;
    std::vector<std::string> m_args;
    std::optional<std::filesystem::path> m_cd;
    std::map<std::string, std::string> m_envOverride;
};
} // namespace crew
#endif
