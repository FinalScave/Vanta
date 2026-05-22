#include "workspace/git_service_impl.h"

#include <utility>

#include "vanta/platform/process.h"

namespace vanta {

void internal::GitServiceImpl::SetWorkspaceRoot(std::filesystem::path workspace_root) {
    workspace_root_ = std::move(workspace_root);
}

GitDiff internal::GitServiceImpl::Diff() const {
    return Diff(workspace_root_);
}

GitDiff internal::GitServiceImpl::Diff(const std::filesystem::path& workspace_root) const {
    CommandResult result = RunCommand({
        .executable = "git",
        .arguments = {"diff", "--"},
        .working_directory = workspace_root,
    });
    return {
        .exit_code = result.exit_code,
        .text = result.standard_output + result.standard_error,
    };
}

}
