#pragma once

#include "vanta/platform/async.h"
#include "vanta/plugin/approval_service.h"
#include "vanta/workspace/settings_service.h"
#include "vanta/vfs/virtual_file_system.h"

namespace vanta {

class IdeEnvironment {
public:
    AsyncRuntime async;
    VirtualFileSystem vfs;
    ApprovalService approvals;
    SettingsService globalSettings;
};

}
