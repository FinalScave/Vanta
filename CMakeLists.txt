cmake_minimum_required(VERSION 3.20)

project(Vanta VERSION 0.1.0 LANGUAGES CXX)

include(cmake/VantaOptions.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(vanta_core
    src/agent/agent_operation.cpp
    src/agent/agent_tool_registry.cpp
    src/agent/agent_context.cpp
    src/execution/build_service.cpp
    src/execution/job_service.cpp
    src/execution/problem_matcher.cpp
    src/workspace/capability_service.cpp
    src/workspace/change_set_service.cpp
    src/builtin/cmake/cmake_build_provider.cpp
    src/builtin/cmake/cmake_project_model.cpp
    src/builtin/clice/clice_integration.cpp
    src/language/code_intelligence_service.cpp
    src/workspace/command_palette.cpp
    src/workspace/diagnostic_service.cpp
    src/workspace/document_service.cpp
    src/workspace/editor.cpp
    src/core/registration.cpp
    src/core/text.cpp
    src/workspace/workspace.cpp
    src/execution/execution_service.cpp
    src/execution/execution_protocol.cpp
    src/builtin/git/git_client.cpp
    src/workspace/workspace_context.cpp
    src/workspace/ide_event.cpp
    src/workspace/ide_application.cpp
    src/workspace/index_service.cpp
    src/workspace/initialization.cpp
    src/workspace/workspace_runtime.cpp
    src/workspace/workspace_services.cpp
    src/builtin/cpp/cpp_index.cpp
    src/language/document_language_sync.cpp
    src/language/language_service.cpp
    src/language/language_request_pipeline.cpp
    src/language/lsp_language_service.cpp
    src/language/lsp_client.cpp
    src/platform/async.cpp
    src/platform/json.cpp
    src/platform/process_posix.cpp
    src/plugin/contribution_registry.cpp
    src/plugin/core_plugins.cpp
    src/plugin/extension_context.cpp
    src/plugin/permissions.cpp
    src/plugin/plugin_process_host.cpp
    src/plugin/plugin_protocol.cpp
    src/plugin/plugin_manager.cpp
    src/project/component.cpp
    src/project/project.cpp
    src/project/project_manager.cpp
    src/project/project_state_store.cpp
    src/project/project_template.cpp
    src/execution/run_configuration.cpp
    src/plugin/approval_service.cpp
    src/workspace/settings_service.cpp
    src/workspace/search_service.cpp
    src/workspace/layout_state_store.cpp
    src/workspace/ui_state_store.cpp
    src/vfs/uri.cpp
    src/vfs/file_watcher.cpp
    src/vfs/virtual_file.cpp
    src/vfs/virtual_file_system.cpp
)

target_include_directories(vanta_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(vanta_core PUBLIC cxx_std_20)

if(APPLE)
    target_link_libraries(vanta_core PUBLIC "-framework CoreServices")
endif()

if(VANTA_ENABLE_WARNINGS)
    target_compile_options(vanta_core PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endif()

add_executable(vanta src/app/main.cpp)
target_link_libraries(vanta PRIVATE vanta_core)

if(VANTA_BUILD_TESTS)
    enable_testing()
    add_executable(vanta_tests tests/vanta_tests.cpp)
    target_link_libraries(vanta_tests PRIVATE vanta_core)
    add_test(NAME vanta_tests COMMAND vanta_tests)
endif()
