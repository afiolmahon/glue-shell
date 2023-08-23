/**
 * Wrapper for running an external commands and obtaining its output
 */
#ifndef CREW_COMMAND_HPP
#define CREW_COMMAND_HPP

#include "util.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace crew {

/** Wait for a child process to exit and return its exit code */
inline int childExit(int pid)
{
    int status{};
    if (::waitpid(pid, &status, 0) == -1) {
        fatal("waitpid failed: ", std::strerror(errno));
    }

    if (!WIFEXITED(status)) {
        fatal("child failed to exit normally");
    }

    return WEXITSTATUS(status);
}

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

    int runPty(std::ostream& outStr = std::cout) const
    {
        if (m_verbose) {
            describe();
        }
        int amaster{};

        int pid = ::forkpty(&amaster, /*name*/ nullptr, nullptr, nullptr);

        if (pid == -1) { // error
            fatal("fork() failed");
        }
        if (pid == 0) { // child
            if (m_cd.has_value()) {
                current_path(*m_cd);
            }

            // setup subprocess specific environment variables
            for (const auto& [k, v] : m_envOverride) {
                if (::setenv(k.c_str(), v.c_str(), 1) == -1) {
                    fatal("failed to update environment variable ", k, ": ", std::strerror(errno));
                }
            }

            std::string command = m_command;
            auto args = m_args;

            std::vector<char*> argv{command.data()};
            for (auto& a : args) {
                argv.push_back(a.data());
            }
            argv.push_back(nullptr);

            if (::execvp(m_command.c_str(), argv.data()) == -1) {
                fatal("execvp failed: ", std::strerror(errno));
            }
        }

        // parent
        char buffer[4096];

        // TODO: support the input side of the psuedoterminal
        // try this approach: https://rmathew.blogspot.com/2006/09/terminal-sickness.html

        while (1) {
            ssize_t count = ::read(amaster, buffer, sizeof(buffer));
            if (count == -1) {
                if (errno == EINTR) {
                    continue;
                } else if (errno == EIO) { // child side closed
                    break;
                } else {
                    fatal("read() failed: ", std::strerror(errno));
                }
            } else if (count == 0) {
                break;
            } else {
                outStr << std::string_view(buffer, count);
            }
        }
        ::close(amaster);
        outStr.flush();

        return childExit(pid);
    }

    int run(std::ostream& outStr = std::cout, std::ostream& errStr = std::cerr) const
    {
        if (m_verbose) {
            describe();
        }
        int outPipe[2]; // exit, entrance
        if (::pipe(outPipe) == -1) {
            fatal("failed to create outPipe");
        }

        int errPipe[2]; // exit, entrance
        if (::pipe(errPipe) == -1) {
            fatal("failed to create errPipe");
        }

        int pid = ::fork();

        if (pid == -1) { // error
            fatal("fork() failed");
        }
        if (pid == 0) { // child
            if (m_cd.has_value()) {
                current_path(*m_cd);
            }

            // setup subprocess specific environment variables
            for (const auto& [k, v] : m_envOverride) {
                if (::setenv(k.c_str(), v.c_str(), 1) == -1) {
                    fatal("failed to update environment variable ", k, ": ", std::strerror(errno));
                }
            }

            while ((::dup2(outPipe[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {
            }
            while ((::dup2(errPipe[1], STDERR_FILENO) == -1) && (errno == EINTR)) {
            }
            ::close(outPipe[0]);
            ::close(errPipe[0]);

            std::string command = m_command;
            auto args = m_args;

            std::vector<char*> argv{command.data()};
            for (auto& a : args) {
                argv.push_back(a.data());
            }
            argv.push_back(nullptr);

            if (::execvp(m_command.c_str(), argv.data()) == -1) {
                fatal("execvp failed: ", std::strerror(errno));
            }
        }

        // parent
        ::close(outPipe[1]);
        ::close(errPipe[1]);

        char buffer[4096];
        while (1) {
            ssize_t count = ::read(outPipe[0], buffer, sizeof(buffer));
            if (count == -1) {
                if (errno == EINTR) {
                    continue;
                } else {
                    fatal("read() failed");
                }
            } else if (count == 0) {
                break;
            } else {
                outStr << std::string_view(buffer, count);
            }
        }
        ::close(outPipe[0]);
        outStr.flush();

        while (1) {
            ssize_t count = ::read(errPipe[0], buffer, sizeof(buffer));
            if (count == -1) {
                if (errno == EINTR) {
                    continue;
                } else {
                    fatal("read() failed");
                }
            } else if (count == 0) {
                break;
            } else {
                errStr << std::string_view(buffer, count);
            }
        }
        ::close(errPipe[0]);
        errStr.flush();

        return childExit(pid);
    }

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
