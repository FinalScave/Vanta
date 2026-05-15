#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vanta {

struct CommandResult {
    int exitCode = -1;
    std::string standardOutput;
    std::string standardError;
};

struct CommandSpec {
    std::string executable;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
};

struct CommandCallbacks {
    std::function<void(const std::string&)> onStdout;
    std::function<void(const std::string&)> onStderr;
};

CommandResult runCommand(const CommandSpec& spec, CommandCallbacks callbacks = {});

class ChildProcess {
public:
    ChildProcess() = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;
    ~ChildProcess();

    bool start(const CommandSpec& spec, std::string* errorMessage = nullptr);
    bool running() const;
    std::optional<int> tryWait();
    int wait();
    bool writeStdin(const std::string& text);
    std::string readStdoutAvailable();
    std::string readStderrAvailable();
    void terminate();
    std::optional<int> exitCode() const;

private:
    void closePipes();
    void rememberExitStatus(int status);

    int pid_ = -1;
    int stdinFd_ = -1;
    int stdoutFd_ = -1;
    int stderrFd_ = -1;
    std::optional<int> exitCode_;
};

}
