#include "test_support.h"

namespace vanta::tests {

void TestDebugService() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    session.Context().Debug().RegisterProvider(std::make_unique<FakeDebugProvider>());

    const vanta::BreakpointId breakpoint = session.Context().Debug().AddBreakpoint({
        .file = session.Context().CurrentWorkspace().File("main.cpp"),
        .line = 1,
    });
    REQUIRE(breakpoint != 0);
    REQUIRE(session.Context().Debug().BreakpointsForFile(session.Context().CurrentWorkspace().File("main.cpp")).size() == 1);

    int events = 0;
    vanta::DebugSession debug_session = session.Context().Debug().Start(session.Context(), {
        .id = "debug.main",
        .name = "Debug Main",
        .type = "native",
    }, [&](const vanta::DebugEvent&) {
        ++events;
    });
    REQUIRE(debug_session.status == vanta::DebugSessionStatus::Running);
    REQUIRE(events == 1);
    REQUIRE(session.Context().Debug().Evaluate(debug_session.id, "value").ok);
    REQUIRE(session.Context().Debug().StackTrace(debug_session.id).size() == 1);
    REQUIRE(session.Context().Debug().Variables(debug_session.id, 1).size() == 1);
    REQUIRE(session.Context().Debug().Pause(debug_session.id));
    REQUIRE(session.Context().Debug().Session(debug_session.id)->status == vanta::DebugSessionStatus::Paused);
    REQUIRE(session.Context().Debug().Stop(debug_session.id));
    REQUIRE(session.Context().Debug().Session(debug_session.id)->status == vanta::DebugSessionStatus::Stopped);
    session.Close();
}

}

TEST_CASE("Debug service", "[debug]") {
    vanta::tests::TestDebugService();
}
