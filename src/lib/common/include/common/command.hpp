/**
 * Wrapper for running an external command and obtaining output
 */
#ifndef CREW_COMMAND_HPP
#define CREW_COMMAND_HPP

#include <common/util.hpp>

#include <concepts>
#include <filesystem>
#include <functional>
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

enum class RunMode {
    Block,
    BlockPty,
    ExecPty,
};

class [[nodiscard]] Command {
public:
    template <typename... Args>
    explicit Command(std::string command, Args... arguments) :
        m_command(std::move(command))
    {
        args(std::forward<Args>(arguments)...);
    }

    template <std::convertible_to<std::string> First, typename... Rest>
    Command& args(First&& first, Rest&&... rest) &
    {
        m_args.emplace_back(std::forward<First>(first));
        args<Rest...>(std::forward<Rest>(rest)...);
        return *this;
    }
    template <std::convertible_to<std::string> First, typename... Rest>
    Command args(First&& first, Rest&&... rest) &&
    {
        return std::move(this->args(std::forward<First>(first), std::forward<Rest>(rest)...));
    }

    template <std::output_iterator<std::string_view> Begin, std::sentinel_for<Begin> End>
    Command& args(Begin begin, End end) &
    {
        for (auto it = begin; it != end; ++it) {
            m_args.push_back(*it);
        }
        return *this;
    }
    template <std::output_iterator<std::string_view> Begin, std::sentinel_for<Begin> End>
    Command args(Begin begin, End end) &&
    {
        return std::move(this->args(begin, end));
    }

    Command& setEnv(const std::string& k, std::string value) &
    {
        m_envOverride[k] = std::move(value);
        return *this;
    }
    Command setEnv(const std::string& k, std::string value) &&
    {
        return std::move(this->setEnv(k, std::move(value)));
    }

    Command& setCurrentDir(std::optional<std::filesystem::path> directory) &
    {
        m_cd = std::move(directory);
        return *this;
    }
    Command setCurrentDir(std::optional<std::filesystem::path> directory) &&
    {
        return std::move(this->setCurrentDir(std::move(directory)));
    }

    Command& setVerbose(bool verbose) &
    {
        m_verbose = verbose;
        return *this;
    }
    Command setVerbose(bool verbose) && { return std::move(this->setVerbose(verbose)); }

    Command& setDry(bool dry) &
    {
        m_dryRun = dry;
        return *this;
    }
    Command setDry(bool dry) && { return std::move(this->setDry(dry)); }

    Command& setOut(std::ostream& str) &
    {
        m_getOut = [&str]() -> std::ostream& { return str; };
        return *this;
    }
    Command setOut(std::ostream& str) && { return std::move(this->setOut(str)); }

    Command& setErr(std::ostream& str) &
    {
        m_getErr = [&str]() -> std::ostream& { return str; };
        return *this;
    }
    Command setErr(std::ostream& str) && { return std::move(this->setErr(str)); }

    Command& onError(OnError onError) &
    {
        m_onError = onError;
        return *this;
    }
    Command onError(OnError onError) && { return std::move(this->onError(onError)); }

    /** Execute the child process, block until it finishes and return its exit code */
    int run(RunMode mode = RunMode::Block);

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
    std::ostream& outStream() { return m_getOut(); }
    std::ostream& errStream() { return m_getErr(); }

    /** Use pipes to receive child stdout, stderr */
    int runPipe();
    /** Use a pty to receive child stdout */
    int runPty();
    /** Replace the current process with the command */
    [[noreturn]] int execPty();

    /** Called from the child process to replace the current process with the specified command */
    void replaceProcessImage();

private:
    // recursive base case for the args(T...) methods
    template <typename None = void>
    Command& args() &
    {
        return *this;
    }

    // streams to populate with child process output
    std::function<std::ostream&()> m_getOut = []() -> std::ostream& {
        return std::cout;
    };

    std::function<std::ostream&()> m_getErr = []() -> std::ostream& {
        return std::cerr;
    };

    OnError m_onError = OnError::Fatal;
    bool m_verbose{};
    bool m_dryRun{};
    std::string m_command;
    std::vector<std::string> m_args;
    std::optional<std::filesystem::path> m_cd;
    std::map<std::string, std::string> m_envOverride;
};
} // namespace crew
#endif
