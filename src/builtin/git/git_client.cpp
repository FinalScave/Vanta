#include "vanta/builtin/git/git_client.h"

#include "vanta/platform/process.h"

namespace vanta {

GitDiff GitClient::diff(const std::filesystem::path& workspaceRoot) const {
    CommandResult result = runCommand({
        .executable = "git",
        .arguments = {"diff", "--"},
        .workingDirectory = workspaceRoot,
    });
    return {
        .exitCode = result.exitCode,
        .text = result.standardOutput + result.standardError,
    };
}

}
