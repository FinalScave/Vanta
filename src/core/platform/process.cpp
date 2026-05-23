#include "mornox/platform/process.h"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace mornox {

CommandResult RunCommand(const CommandSpec& spec, CommandCallbacks callbacks) {
    ChildProcess process;
    std::string error;
    if (!process.Start(spec, &error)) {
        throw std::runtime_error(error.empty() ? "Failed to start process" : error);
    }

    CommandResult result;
    auto drain_output = [&] {
        std::string stdout_chunk = process.ReadStdoutAvailable();
        if (!stdout_chunk.empty()) {
            if (callbacks.on_stdout) {
                callbacks.on_stdout(stdout_chunk);
            }
            result.standard_output += std::move(stdout_chunk);
        }

        std::string stderr_chunk = process.ReadStderrAvailable();
        if (!stderr_chunk.empty()) {
            if (callbacks.on_stderr) {
                callbacks.on_stderr(stderr_chunk);
            }
            result.standard_error += std::move(stderr_chunk);
        }
    };

    while (true) {
        drain_output();
        if (auto exit_code = process.TryWait()) {
            result.exit_code = *exit_code;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    drain_output();
    return result;
}

}
