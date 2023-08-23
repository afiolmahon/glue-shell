/**
 * Wrapper for running an external commands and obtaining its output
 */
#ifndef CREW_COMMAND_HPP
#define CREW_COMMAND_HPP

#include "util.hpp"

#include <concepts>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace crew {

enum class OnError {
    Fatal = 0,
    Return,
};

class [[nodiscard]] Command {
public:
    template <typename... Args>
    explicit Command(std::string command, Args... arguments) :
        m_command(std::move(command))
    {
        args(std::forward<Args>(arguments)...);
    }

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

    template <std::convertible_to<std::string> First, typename... Rest>
    Command& args(First&& first, Rest&&... rest) &
    {
        m_args.emplace_back(std::forward<First>(first));
        return args<Rest...>(std::forward<Rest>(rest)...);
    }
    template <std::convertible_to<std::string> First, typename... Rest>
    Command args(First&& first, Rest&&... rest) &&
    {
        m_args.emplace_back(std::forward<First>(first));
        return args<Rest...>(std::forward<Rest>(rest)...);
    }

    template <InputIteratorOf<std::string> Begin, std::sentinel_for<Begin> End>
    Command& args(Begin begin, End end) &
    {
        for (auto it = begin; it != end; ++it) {
            m_args.push_back(*it);
        }
        return *this;
    }
    template <InputIteratorOf<std::string> Begin, std::sentinel_for<Begin> End>
    Command args(Begin begin, End end) &&
    {
        for (auto it = begin; it != end; ++it) {
            m_args.push_back(*it);
        }
        return std::move(*this);
    }

    template <RangeOf<std::string> Range>
    Command args(Range&& range) &&
    {
        return args(range.begin(), range.end());
    }
    template <RangeOf<std::string> Range>
    Command& args(Range&& range) &
    {
        return args(range.begin(), range.end());
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

    Command& setOut(std::ostream& str) &
    {
        m_outStream = str;
        return *this;
    }
    Command setOut(std::ostream& str) &&
    {
        m_outStream = str;
        return std::move(*this);
    }

    Command& setErr(std::ostream& str) &
    {
        m_errStream = str;
        return *this;
    }
    Command setErr(std::ostream& str) &&
    {
        m_errStream = str;
        return std::move(*this);
    }

    /** Control whether or not run() emulates a terminal device */
    Command& usePty(bool use = true) &
    {
        m_usePty = use;
        return *this;
    }
    Command usePty(bool use = true) &&
    {
        m_usePty = use;
        return std::move(*this);
    }

    Command& onError(OnError onError) &
    {
        m_onError = onError;
        return *this;
    }
    Command onError(OnError onError) &&
    {
        m_onError = onError;
        return std::move(*this);
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

    /** Execute the child process, block until it finishes and return its exit code */
    int run()
    {
        int result = m_usePty ? runPty() : runPipe();
        if (result != 0) {
            switch (m_onError) {
            case OnError::Return:
                break; // return the result
            case OnError::Fatal:
                fatal("command \"", toString(), "\" failed with non-zero exit status: ", result);
            }
        }
        return result;
    }

    /** Report the command line as a string */
    std::string toString() const
    {
        std::string result = m_command;
        for (const auto& a : m_args) {
            result += " ";
            result += a;
        }
        return result;
    }

protected:
    std::ostream& outStream() { return m_outStream.get(); }
    std::ostream& errStream() { return m_errStream.get(); }

    /** Use pipes to receive child stdout, stderr */
    int runPipe();
    /** Use a pty to receive child stdout */
    int runPty();

private:
    // recursive base case for the args(T...) methods
    template <typename None = void>
    Command& args() &
    {
        return *this;
    }
    template <typename None = void>
    Command args() &&
    {
        return std::move(*this);
    }

    // streams to populate with child process output
    std::reference_wrapper<std::ostream> m_outStream = std::cout;
    std::reference_wrapper<std::ostream> m_errStream = std::cerr;

    OnError m_onError = OnError::Fatal;
    bool m_verbose{};
    bool m_usePty{};
    std::string m_command;
    std::vector<std::string> m_args;
    std::optional<std::filesystem::path> m_cd;
    std::map<std::string, std::string> m_envOverride;
};
} // namespace crew
#endif
