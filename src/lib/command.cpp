#include "include/command.hpp"

#include <cstring>

#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace crew {
namespace {
/** Wait for a child process to exit and return its exit code */
int childExit(int pid)
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
} // namespace

int Command::run()
{
    // if verbose flag is set, print details about the command being executed
    if (m_verbose || m_dryRun) {
        auto& str = std::cerr;
        str << (m_dryRun ? "DRY: " : "LOG: ") << toString() << "\n";
        if (m_cd.has_value()) {
            str << "\t- executing from directory: " << *m_cd << "\n";
        }
        if (!m_envOverride.empty()) {
            str << "\t- overriding "
                << m_envOverride.size() << " environment variables\n";
        }
        str.flush();
    }

    if (m_dryRun) { // for dry run, we just want to know what would have been executed
        return 0;
    }

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

int Command::runPipe()
{
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
            outStream() << std::string_view(buffer, count);
        }
    }
    ::close(outPipe[0]);
    outStream().flush();

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
            errStream() << std::string_view(buffer, count);
        }
    }
    ::close(errPipe[0]);
    errStream().flush();

    return childExit(pid);
}

int Command::runPty()
{
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
            outStream() << std::string_view(buffer, count);
        }
    }
    ::close(amaster);
    outStream().flush();

    return childExit(pid);
}

} // namespace crew
