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

    // TODO: variadic constructor for taking args in parameter pack

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

    Command& args(std::initializer_list<std::string> args) &
    {
        for (auto& a : args) {
            arg(a);
        }
        return *this;
    }
    Command args(std::initializer_list<std::string> args) &&
    {
        for (auto& a : args) {
            arg(a);
        }
        return std::move(*this);
    }

    // template <typename Arg, typename... Args>
    // Command& args(Arg&& a, Args&&... aa) &
    // {
    //     if constexpr (sizeof...(aa) > 0) {
    //         arg(std::move(a));
    //     } else {
    //         args(std::forward<Args>(aa)...);
    //     }
    //     return *this;
    // }
    // template <typename Arg, typename... Args>
    // Command args(Arg&& a, Args&&... aa) &&
    // {
    //     if constexpr (sizeof...(aa) > 0) {
    //         arg(std::move(a));
    //     } else {
    //         args(std::forward<Args>(aa)...);
    //     }
    //     return std::move(*this);
    // }

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

    void tryRun(std::ostream& outStr = std::cout, std::ostream& errStr = std::cerr) const
    {
        if (int r = run(outStr, errStr); r != 0) {
            throw std::runtime_error("run exited with status " + std::to_string(r));
        }
    }

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
    bool m_verbose{};
    std::string m_command;
    std::vector<std::string> m_args;
    std::optional<std::filesystem::path> m_cd;
    std::map<std::string, std::string> m_envOverride;
};
} // namespace crew
#endif
