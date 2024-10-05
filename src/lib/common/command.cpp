#include <common/command.hpp>

#include <cstring>

#include <fcntl.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <utmp.h>

namespace fs = std::filesystem;

namespace crew {
namespace {
/** Wait for a child process to exit and return its exit code */
int childExit(int pid)
{
    int status{};
    if (::waitpid(pid, &status, 0) == -1) {
        fatal("waitpid failed: {}", std::strerror(errno));
    }

    if (!WIFEXITED(status)) {
        fatal("child failed to exit normally");
    }

    return WEXITSTATUS(status);
}

/**
 * @param fd - fd to read data from
 * @param dest - stream to write data to
 *
 * @fatal - error occurs and errno is set to neither EINTR, EIO
 */
void pumpFdToStream(int fd, std::ostream& dest)
{
    thread_local char buffer[4096];

    while (1) {
        ssize_t count = ::read(fd, buffer, sizeof(buffer));
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
            dest << std::string_view(buffer, count);
        }
    }
    dest.flush();
}

struct FdPair {
    // order matters
    int exit{};
    int entrance{};

    static FdPair openPipe()
    {
        FdPair result{};
        if (::pipe(&result.exit) == -1) {
            fatal("failed to open pipe");
        }
        return result;
    }
};
static_assert(sizeof(FdPair) == sizeof(int[2]));
} // namespace

int Command::run(RunMode mode)
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

    int result{};
    switch (mode) {
    case RunMode::Block:
        result = runPipe();
        break;
    case RunMode::BlockPty:
        result = runPty();
        break;
    case RunMode::ExecPty:
        result = execPty();
        break;
    }

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

    auto outPipe = FdPair::openPipe();
    auto errPipe = FdPair::openPipe();

    int pid = ::fork();

    if (pid == -1) { // error
        fatal("fork() failed");
    }
    if (pid == 0) { // child
        while ((::dup2(outPipe.entrance, STDOUT_FILENO) == -1) && (errno == EINTR)) {
        }
        while ((::dup2(errPipe.entrance, STDERR_FILENO) == -1) && (errno == EINTR)) {
        }
        ::close(outPipe.exit);
        ::close(errPipe.exit);

        replaceProcessImage();
    }

    // parent
    ::close(outPipe.entrance);
    ::close(errPipe.entrance);

    pumpFdToStream(outPipe.exit, outStream());
    ::close(outPipe.exit);

    pumpFdToStream(errPipe.exit, errStream());
    ::close(errPipe.exit);

    return childExit(pid);
}

// TODO(antonio): make this work as smoothly as execPty
// this may help
// https://gist.github.com/zmwangx/2bac2af9195cad47069419ccd9ee98d8
// TODO test input side of psuedoterminal works
// try this approach: https://rmathew.blogspot.com/2006/09/terminal-sickness.html
int Command::runPty()
{
    int amaster{};

    int pid = ::forkpty(&amaster, /*name*/ nullptr, nullptr, nullptr);

    if (pid == -1) { // error
        fatal("fork() failed");
    }
    if (pid == 0) { // child
        replaceProcessImage();
    }

    // parent
    pumpFdToStream(amaster, outStream());
    ::close(amaster);

    return childExit(pid);
}

[[noreturn]] int Command::execPty()
{
    // open the psuedoterminal
    int fd = ::open("/dev/tty", 0);
    ::login_tty(fd);

    replaceProcessImage();
    fatal("unreachable");
}

void Command::replaceProcessImage()
{
    if (m_cd.has_value()) {
        current_path(*m_cd);
    }

    // setup subprocess specific environment variables
    for (const auto& [k, v] : m_envOverride) {
        if (::setenv(k.c_str(), v.c_str(), 1) == -1) {
            fatal("failed to update environment variable ", k, ": ", std::strerror(errno));
        }
    }

    std::vector<char*> argv{m_command.data()};
    for (auto& arg : m_args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    if (::execvp(m_command.c_str(), argv.data()) == -1) {
        fatal("execvp failed: ", std::strerror(errno));
    }
}

} // namespace crew
