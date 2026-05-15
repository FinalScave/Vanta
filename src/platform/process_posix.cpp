#include "vanta/platform/process.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <stdexcept>
#include <sys/select.h>
#include <sys/wait.h>
#include <utility>
#include <unistd.h>

namespace vanta {
namespace {

std::vector<char*> buildArgv(const CommandSpec& spec) {
    std::vector<char*> argv;
    argv.reserve(spec.arguments.size() + 2);
    argv.push_back(const_cast<char*>(spec.executable.c_str()));
    for (const std::string& argument : spec.arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

void setCloseOnExec(int fd) {
    if (fd >= 0) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
}

void setNonBlocking(int fd) {
    if (fd >= 0) {
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

std::string readAvailable(int fd) {
    if (fd < 0) {
        return {};
    }

    std::string result;
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t count = read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            result.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    return result;
}

}

CommandResult runCommand(const CommandSpec& spec, CommandCallbacks callbacks) {
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0) {
        throw std::runtime_error("Failed to create command pipes");
    }

    const pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("Failed to fork process");
    }

    if (pid == 0) {
        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);

        if (!spec.workingDirectory.empty()) {
            chdir(spec.workingDirectory.c_str());
        }

        std::vector<char*> argv = buildArgv(spec);
        execvp(spec.executable.c_str(), argv.data());
        _exit(127);
    }

    close(stdoutPipe[1]);
    close(stderrPipe[1]);
    setNonBlocking(stdoutPipe[0]);
    setNonBlocking(stderrPipe[0]);

    int status = 0;
    bool exited = false;
    std::string stdoutText;
    std::string stderrText;

    auto drainOutput = [&] {
        std::string stdoutChunk = readAvailable(stdoutPipe[0]);
        if (!stdoutChunk.empty()) {
            if (callbacks.onStdout) {
                callbacks.onStdout(stdoutChunk);
            }
            stdoutText += std::move(stdoutChunk);
        }
        std::string stderrChunk = readAvailable(stderrPipe[0]);
        if (!stderrChunk.empty()) {
            if (callbacks.onStderr) {
                callbacks.onStderr(stderrChunk);
            }
            stderrText += std::move(stderrChunk);
        }
    };

    while (!exited) {
        drainOutput();

        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            exited = true;
        } else if (waited < 0 && errno != EINTR) {
            exited = true;
        } else {
            usleep(10000);
        }
    }
    drainOutput();

    CommandResult result;
    result.standardOutput = std::move(stdoutText);
    result.standardError = std::move(stderrText);
    close(stdoutPipe[0]);
    close(stderrPipe[0]);

    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
    }
    return result;
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept {
    *this = std::move(other);
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        terminate();
        pid_ = other.pid_;
        stdinFd_ = other.stdinFd_;
        stdoutFd_ = other.stdoutFd_;
        stderrFd_ = other.stderrFd_;
        exitCode_ = other.exitCode_;
        other.pid_ = -1;
        other.stdinFd_ = -1;
        other.stdoutFd_ = -1;
        other.stderrFd_ = -1;
        other.exitCode_.reset();
    }
    return *this;
}

ChildProcess::~ChildProcess() {
    terminate();
}

bool ChildProcess::start(const CommandSpec& spec, std::string* errorMessage) {
    terminate();
    exitCode_.reset();

    int stdinPipe[2] = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    if (pipe(stdinPipe) != 0 || pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create process pipes";
        }
        return false;
    }

    setCloseOnExec(stdinPipe[1]);
    setCloseOnExec(stdoutPipe[0]);
    setCloseOnExec(stderrPipe[0]);

    const pid_t pid = fork();
    if (pid < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to fork process";
        }
        return false;
    }

    if (pid == 0) {
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        close(stdinPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);

        if (!spec.workingDirectory.empty()) {
            chdir(spec.workingDirectory.c_str());
        }

        std::vector<char*> argv = buildArgv(spec);
        execvp(spec.executable.c_str(), argv.data());
        _exit(127);
    }

    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    close(stderrPipe[1]);
    pid_ = static_cast<int>(pid);
    stdinFd_ = stdinPipe[1];
    stdoutFd_ = stdoutPipe[0];
    stderrFd_ = stderrPipe[0];
    setNonBlocking(stdoutFd_);
    setNonBlocking(stderrFd_);
    return true;
}

bool ChildProcess::running() const {
    if (pid_ < 0) {
        return false;
    }
    return kill(pid_, 0) == 0 || errno == EPERM;
}

std::optional<int> ChildProcess::tryWait() {
    if (pid_ < 0) {
        return exitCode_;
    }
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) {
        return std::nullopt;
    }
    if (result == pid_) {
        rememberExitStatus(status);
        pid_ = -1;
        return exitCode_;
    }
    if (result < 0 && errno == ECHILD) {
        pid_ = -1;
        return exitCode_;
    }
    return std::nullopt;
}

int ChildProcess::wait() {
    if (pid_ < 0) {
        return exitCode_.value_or(-1);
    }
    int status = 0;
    while (true) {
        const pid_t result = waitpid(pid_, &status, 0);
        if (result == pid_) {
            rememberExitStatus(status);
            pid_ = -1;
            return exitCode_.value_or(-1);
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 && errno == ECHILD) {
            pid_ = -1;
            return exitCode_.value_or(-1);
        }
        return -1;
    }
}

bool ChildProcess::writeStdin(const std::string& text) {
    if (stdinFd_ < 0) {
        return false;
    }
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const ssize_t count = write(stdinFd_, data, remaining);
        if (count > 0) {
            data += count;
            remaining -= static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

std::string ChildProcess::readStdoutAvailable() {
    return readAvailable(stdoutFd_);
}

std::string ChildProcess::readStderrAvailable() {
    return readAvailable(stderrFd_);
}

void ChildProcess::terminate() {
    if (pid_ >= 0) {
        kill(pid_, SIGTERM);
        int status = 0;
        if (waitpid(pid_, &status, 0) == pid_) {
            rememberExitStatus(status);
        } else {
            exitCode_ = 143;
        }
        pid_ = -1;
    }
    closePipes();
}

std::optional<int> ChildProcess::exitCode() const {
    return exitCode_;
}

void ChildProcess::closePipes() {
    if (stdinFd_ >= 0) {
        close(stdinFd_);
        stdinFd_ = -1;
    }
    if (stdoutFd_ >= 0) {
        close(stdoutFd_);
        stdoutFd_ = -1;
    }
    if (stderrFd_ >= 0) {
        close(stderrFd_);
        stderrFd_ = -1;
    }
}

void ChildProcess::rememberExitStatus(int status) {
    if (WIFEXITED(status)) {
        exitCode_ = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exitCode_ = 128 + WTERMSIG(status);
    } else {
        exitCode_ = -1;
    }
}

}
