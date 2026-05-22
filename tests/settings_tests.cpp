#include "test_support.h"
#include "vanta/plugin/plugin_storage.h"

namespace vanta::tests {

void TestSettingsAndResult() {
    const auto root = MakeTempRoot();
    vanta::SettingsService settings;
    vanta::RegisterDefaultSettings(settings);
    const vanta::SettingScope ide_scope{.kind = vanta::SettingScopeKind::Ide};
    const vanta::SettingScope workspace_scope{.kind = vanta::SettingScopeKind::Workspace, .qualifier = root.string()};
    const vanta::SettingScope language_scope{.kind = vanta::SettingScopeKind::Language, .qualifier = "cpp"};
    int setting_events = 0;
    settings.OnDidChangeSetting([&](const vanta::SettingChangeEvent& event) {
        if (event.id == "editor.formatOnSave") {
            ++setting_events;
        }
    });
    REQUIRE(settings.SetValue("editor.fontSize", ide_scope, vanta::SettingValue::IntValue(16)));
    REQUIRE(settings.SetValue("editor.formatOnSave", workspace_scope, vanta::SettingValue::BoolValue(true)));
    REQUIRE(settings.SetValue("editor.formatOnSave", language_scope, vanta::SettingValue::BoolValue(false)));
    const vanta::SettingResolution resolved = settings.Resolve("editor.formatOnSave", {
        .workspace_id = root.string(),
        .language_id = "cpp",
    });
    REQUIRE(!resolved.defaulted);
    REQUIRE(resolved.source.kind == vanta::SettingScopeKind::Language);
    REQUIRE(std::get<bool>(resolved.value.data) == false);
    const auto scopes = settings.ScopesFor("editor.formatOnSave", {
        .workspace_id = root.string(),
        .language_id = "cpp",
    });
    REQUIRE(scopes.size() == 3);
    REQUIRE(std::any_of(scopes.begin(), scopes.end(), [](const vanta::SettingScopeDescriptor& scope) {
        return scope.scope.kind == vanta::SettingScopeKind::Language && scope.effective_source;
    }));
    const auto search_results = settings.Search("model");
    REQUIRE(!search_results.empty());
    REQUIRE(std::any_of(search_results.begin(), search_results.end(), [](const vanta::SettingSearchResult& result) {
        return result.setting_id == "ai.agent.model";
    }));
    REQUIRE(!settings.Children("ai").empty());
    REQUIRE(settings.Save(workspace_scope, root / ".vanta" / "settings.json"));

    vanta::SettingsService loaded;
    vanta::RegisterDefaultSettings(loaded);
    REQUIRE(loaded.Load(workspace_scope, root / ".vanta" / "settings.json"));
    const vanta::SettingResolution loaded_value = loaded.Resolve("editor.formatOnSave", {
        .workspace_id = root.string(),
        .language_id = "python",
    });
    REQUIRE(std::get<bool>(loaded_value.value.data));
    REQUIRE(setting_events == 2);

    vanta::PluginStorageService storage(root / ".vanta" / "plugin-storage");
    REQUIRE(storage.Write("sample.plugin", "state", vanta::Value::ObjectValue({{"ok", vanta::Value(true)}})));
    const auto state = storage.Read("sample.plugin", "state");
    REQUIRE(state);
    REQUIRE(state.Value()["ok"].AsBool());

    const auto error = vanta::Result<int>::Failure("sample", "failed");
    REQUIRE(!error);
    REQUIRE(error.ErrorValue().code == "sample");
}

}

TEST_CASE("Settings and result", "[settings][platform]") {
    vanta::tests::TestSettingsAndResult();
}
