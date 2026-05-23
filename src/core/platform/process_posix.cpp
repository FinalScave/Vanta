#include "mornox/platform/process.h"

#if !defined(_WIN32)

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace mornox {

namespace internal {

struct ChildProcessState {
    int pid = -1;
    int stdin_fd = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;
    std::optional<int> exit_code;
};

}

namespace {

internal::ChildProcessState& EnsureState(std::unique_ptr<internal::ChildProcessState>& state) {
    if (state == nullptr) {
        state = std::make_unique<internal::ChildProcessState>();
    }
    return *state;
}

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

ChildProcess::ChildProcess() = default;

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : state_(std::move(other.state_)) {}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        Terminate();
        state_ = std::move(other.state_);
    }
    return *this;
}

ChildProcess::~ChildProcess() {
    Terminate();
}

bool ChildProcess::Start(const CommandSpec& spec, std::string* error_message) {
    Terminate();
    internal::ChildProcessState& state = EnsureState(state_);
    state.exit_code.reset();

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
    state.pid = static_cast<int>(pid);
    state.stdin_fd = stdin_pipe[1];
    state.stdout_fd = stdout_pipe[0];
    state.stderr_fd = stderr_pipe[0];
    SetNonBlocking(state.stdout_fd);
    SetNonBlocking(state.stderr_fd);
    return true;
}

bool ChildProcess::Running() const {
    if (state_ == nullptr || state_->pid < 0) {
        return false;
    }
    return kill(state_->pid, 0) == 0 || errno == EPERM;
}

std::optional<int> ChildProcess::TryWait() {
    if (state_ == nullptr || state_->pid < 0) {
        return state_ == nullptr ? std::nullopt : state_->exit_code;
    }
    int status = 0;
    const pid_t result = waitpid(state_->pid, &status, WNOHANG);
    if (result == 0) {
        return std::nullopt;
    }
    if (result == state_->pid) {
        RememberExitStatus(status);
        state_->pid = -1;
        return state_->exit_code;
    }
    if (result < 0 && errno == ECHILD) {
        state_->pid = -1;
        return state_->exit_code;
    }
    return std::nullopt;
}

int ChildProcess::Wait() {
    if (state_ == nullptr || state_->pid < 0) {
        return state_ == nullptr ? -1 : state_->exit_code.value_or(-1);
    }
    int status = 0;
    while (true) {
        const pid_t result = waitpid(state_->pid, &status, 0);
        if (result == state_->pid) {
            RememberExitStatus(status);
            state_->pid = -1;
            return state_->exit_code.value_or(-1);
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 && errno == ECHILD) {
            state_->pid = -1;
            return state_->exit_code.value_or(-1);
        }
        return -1;
    }
}

bool ChildProcess::WriteStdin(const std::string& text) {
    if (state_ == nullptr || state_->stdin_fd < 0) {
        return false;
    }
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const ssize_t count = write(state_->stdin_fd, data, remaining);
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
    return state_ == nullptr ? std::string() : ReadAvailable(state_->stdout_fd);
}

std::string ChildProcess::ReadStderrAvailable() {
    return state_ == nullptr ? std::string() : ReadAvailable(state_->stderr_fd);
}

void ChildProcess::Terminate() {
    if (state_ == nullptr) {
        return;
    }
    if (state_->pid >= 0) {
        kill(state_->pid, SIGTERM);
        int status = 0;
        if (waitpid(state_->pid, &status, 0) == state_->pid) {
            RememberExitStatus(status);
        } else {
            state_->exit_code = 143;
        }
        state_->pid = -1;
    }
    ClosePipes();
}

std::optional<int> ChildProcess::ExitCode() const {
    return state_ == nullptr ? std::nullopt : state_->exit_code;
}

void ChildProcess::ClosePipes() {
    if (state_ == nullptr) {
        return;
    }
    if (state_->stdin_fd >= 0) {
        close(state_->stdin_fd);
        state_->stdin_fd = -1;
    }
    if (state_->stdout_fd >= 0) {
        close(state_->stdout_fd);
        state_->stdout_fd = -1;
    }
    if (state_->stderr_fd >= 0) {
        close(state_->stderr_fd);
        state_->stderr_fd = -1;
    }
}

void ChildProcess::RememberExitStatus(int status) {
    internal::ChildProcessState& state = EnsureState(state_);
    if (WIFEXITED(status)) {
        state.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        state.exit_code = 128 + WTERMSIG(status);
    } else {
        state.exit_code = -1;
    }
}

}

#endif
