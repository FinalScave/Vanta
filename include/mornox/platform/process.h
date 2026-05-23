#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mornox {

namespace internal {
struct ChildProcessState;
}

struct CommandResult {
    int exit_code = -1;
    std::string standard_output;
    std::string standard_error;
};

struct CommandSpec {
    std::string executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
};

struct CommandCallbacks {
    std::function<void(const std::string&)> on_stdout;
    std::function<void(const std::string&)> on_stderr;
};

CommandResult RunCommand(const CommandSpec& spec, CommandCallbacks callbacks = {});

class ChildProcess {
public:
    ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;
    ~ChildProcess();

    bool Start(const CommandSpec& spec, std::string* error_message = nullptr);
    bool Running() const;
    std::optional<int> TryWait();
    int Wait();
    bool WriteStdin(const std::string& text);
    std::string ReadStdoutAvailable();
    std::string ReadStderrAvailable();
    void Terminate();
    std::optional<int> ExitCode() const;

private:
    void ClosePipes();
    void RememberExitStatus(int status);

    std::unique_ptr<internal::ChildProcessState> state_;
};

}
