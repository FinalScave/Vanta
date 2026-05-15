#include "vanta/plugin/extension_context.h"

namespace vanta {

CommandRegistry& ExtensionContext::commands() {
    return workspaceContext().commands();
}

WorkspaceEvents& ExtensionContext::events() {
    return workspaceContext().events();
}

WorkspaceFiles& ExtensionContext::workspaceFiles() {
    return workspaceContext().workspaceFiles();
}

LanguageRegistry& ExtensionContext::languages() {
    return workspaceContext().languages();
}

BuildService& ExtensionContext::build() {
    return workspaceContext().build();
}

ProjectComponents& ExtensionContext::components() {
    return workspaceContext().components();
}

ProjectModelProviderRegistry& ExtensionContext::projectModels() {
    return workspaceContext().projectModels();
}

AgentTools& ExtensionContext::agentTools() {
    return workspaceContext().agentTools();
}

AgentContextProviders& ExtensionContext::agentContext() {
    return workspaceContext().agentContext();
}

AgentOperationService& ExtensionContext::agentOperations() {
    return workspaceContext().agentOperations();
}

AgentOperationJournal& ExtensionContext::agentOperationJournal() {
    return workspaceContext().agentOperationJournal();
}

ExecutionHost& ExtensionContext::execution() {
    return workspaceContext().execution();
}

CodeIntelligenceService& ExtensionContext::codeIntelligence() {
    return workspaceContext().codeIntelligence();
}

JobService& ExtensionContext::jobs() {
    return workspaceContext().jobs();
}

RunConfigurationRegistry& ExtensionContext::runConfigurations() {
    return workspaceContext().runConfigurationRegistry();
}

GitReader& ExtensionContext::git() {
    return workspaceContext().git();
}

IndexCoordinator& ExtensionContext::indexes() {
    return workspaceContext().indexes();
}

CapabilityRegistry& ExtensionContext::capabilities() {
    return workspaceContext().capabilities();
}

WorkspaceInitializationPipeline& ExtensionContext::initialization() {
    return workspaceContext().initialization();
}

ProjectTemplateService& ExtensionContext::projectTemplates() {
    return workspaceContext().projectTemplates();
}

ScratchFileService& ExtensionContext::scratchFiles() {
    return workspaceContext().scratchFiles();
}

ApprovalService& ExtensionContext::approvals() {
    return workspaceContext().approvals();
}

SettingsService& ExtensionContext::settings() {
    return workspaceContext().settings();
}

}
