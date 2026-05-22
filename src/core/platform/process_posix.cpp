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

std::vector<char*> BuildArgv(const CommandSpec& spec) {
    std::vector<char*> argv;
    argv.reserve(spec.arguments.size() + 2);
    argv.push_back(const_cast<char*>(spec.executable.c_str()));
    for (const std::string& argument : spec.arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

void SetCloseOnExec(int fd) {
    if (fd >= 0) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
}

void SetNonBlocking(int fd) {
    if (fd >= 0) {
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

std::string ReadAvailable(int fd) {
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

CommandResult RunCommand(const CommandSpec& spec, CommandCallbacks callbacks) {
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        throw std::runtime_error("Failed to create command pipes");
    }

    const pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("Failed to fork process");
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        if (!spec.working_directory.empty()) {
            chdir(spec.working_directory.c_str());
        }

        std::vector<char*> argv = BuildArgv(spec);
        execvp(spec.executable.c_str(), argv.data());
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    SetNonBlocking(stdout_pipe[0]);
    SetNonBlocking(stderr_pipe[0]);

    int status = 0;
    bool exited = false;
    std::string stdout_text;
    std::string stderr_text;

    auto DrainOutput = [&] {
        std::string stdout_chunk = ReadAvailable(stdout_pipe[0]);
        if (!stdout_chunk.empty()) {
            if (callbacks.on_stdout) {
                callbacks.on_stdout(stdout_chunk);
            }
            stdout_text += std::move(stdout_chunk);
        }
        std::string stderr_chunk = ReadAvailable(stderr_pipe[0]);
        if (!stderr_chunk.empty()) {
            if (callbacks.on_stderr) {
                callbacks.on_stderr(stderr_chunk);
            }
            stderr_text += std::move(stderr_chunk);
        }
    };

    while (!exited) {
        DrainOutput();

        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            exited = true;
        } else if (waited < 0 && errno != EINTR) {
            exited = true;
        } else {
            usleep(10000);
        }
    }
    DrainOutput();

    CommandResult result;
    result.standard_output = std::move(stdout_text);
    result.standard_error = std::move(stderr_text);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept {
    *this = std::move(other);
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        Terminate();
        pid_ = other.pid_;
        stdin_fd_ = other.stdin_fd_;
        stdout_fd_ = other.stdout_fd_;
        stderr_fd_ = other.stderr_fd_;
        exit_code_ = other.exit_code_;
        other.pid_ = -1;
        other.stdin_fd_ = -1;
        other.stdout_fd_ = -1;
        other.stderr_fd_ = -1;
        other.exit_code_.reset();
    }
    return *this;
}

ChildProcess::~ChildProcess() {
    Terminate();
}

bool ChildProcess::Start(const CommandSpec& spec, std::string* error_message) {
    Terminate();
    exit_code_.reset();

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        if (error_message != nullptr) {
            *error_message = "Failed to create process pipes";
        }
        return false;
    }

    SetCloseOnExec(stdin_pipe[1]);
    SetCloseOnExec(stdout_pipe[0]);
    SetCloseOnExec(stderr_pipe[0]);

    const pid_t pid = fork();
    if (pid < 0) {
        if (error_message != nullptr) {
            *error_message = "Failed to fork process";
        }
        return false;
    }

    if (pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        if (!spec.working_directory.empty()) {
            chdir(spec.working_directory.c_str());
        }

        std::vector<char*> argv = BuildArgv(spec);
        execvp(spec.executable.c_str(), argv.data());
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    pid_ = static_cast<int>(pid);
    stdin_fd_ = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];
    stderr_fd_ = stderr_pipe[0];
    SetNonBlocking(stdout_fd_);
    SetNonBlocking(stderr_fd_);
    return true;
}

bool ChildProcess::Running() const {
    if (pid_ < 0) {
        return false;
    }
    return kill(pid_, 0) == 0 || errno == EPERM;
}

std::optional<int> ChildProcess::TryWait() {
    if (pid_ < 0) {
        return exit_code_;
    }
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) {
        return std::nullopt;
    }
    if (result == pid_) {
        RememberExitStatus(status);
        pid_ = -1;
        return exit_code_;
    }
    if (result < 0 && errno == ECHILD) {
        pid_ = -1;
        return exit_code_;
    }
    return std::nullopt;
}

int ChildProcess::Wait() {
    if (pid_ < 0) {
        return exit_code_.value_or(-1);
    }
    int status = 0;
    while (true) {
        const pid_t result = waitpid(pid_, &status, 0);
        if (result == pid_) {
            RememberExitStatus(status);
            pid_ = -1;
            return exit_code_.value_or(-1);
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 && errno == ECHILD) {
            pid_ = -1;
            return exit_code_.value_or(-1);
        }
        return -1;
    }
}

bool ChildProcess::WriteStdin(const std::string& text) {
    if (stdin_fd_ < 0) {
        return false;
    }
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const ssize_t count = write(stdin_fd_, data, remaining);
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

std::string ChildProcess::ReadStdoutAvailable() {
    return ReadAvailable(stdout_fd_);
}

std::string ChildProcess::ReadStderrAvailable() {
    return ReadAvailable(stderr_fd_);
}

void ChildProcess::Terminate() {
    if (pid_ >= 0) {
        kill(pid_, SIGTERM);
        int status = 0;
        if (waitpid(pid_, &status, 0) == pid_) {
            RememberExitStatus(status);
        } else {
            exit_code_ = 143;
        }
        pid_ = -1;
    }
    ClosePipes();
}

std::optional<int> ChildProcess::ExitCode() const {
    return exit_code_;
}

void ChildProcess::ClosePipes() {
    if (stdin_fd_ >= 0) {
        close(stdin_fd_);
        stdin_fd_ = -1;
    }
    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }
    if (stderr_fd_ >= 0) {
        close(stderr_fd_);
        stderr_fd_ = -1;
    }
}

void ChildProcess::RememberExitStatus(int status) {
    if (WIFEXITED(status)) {
        exit_code_ = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code_ = 128 + WTERMSIG(status);
    } else {
        exit_code_ = -1;
    }
}

}
