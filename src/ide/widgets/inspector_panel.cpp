#include "widgets/inspector_panel.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QListWidget>
#include <QSize>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <utility>

#include "mornox/agent/agent_tool_registry.h"
#include "mornox/execution/build_service.h"
#include "mornox/execution/run_configuration.h"
#include "mornox/language/language_service.h"
#include "mornox/workspace/capability_service.h"
#include "mornox/workspace/index_service.h"
#include "mornox/workspace/workspace_context.h"
#include "mornox/workspace/workspace_trust.h"
#include "icons.h"
#include "workspace_controller.h"

namespace mornox::ide {
namespace {

QLabel* SectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("PanelTitle");
    return label;
}

QString IconForLabel(const QString& label) {
    if (label == "Test" || label == "Tests") {
        return "flask-conical";
    }
    if (label == "Symbol" || label == "Languages") {
        return "code";
    }
    if (label == "File") {
        return "file-code";
    }
    if (label == "Job" || label == "Operation" || label == "Run" || label == "Run providers") {
        return "terminal";
    }
    if (label == "Workspace") {
        return "folder";
    }
    if (label == "Diagnostic") {
        return "circle-x";
    }
    if (label == "Index" || label == "Index graph") {
        return "database";
    }
    if (label == "Build providers") {
        return "package";
    }
    if (label == "Agent tools") {
        return "bot";
    }
    if (label == "Workspace trust") {
        return "lock-keyhole";
    }
    if (label == "Inspect failure") {
        return "search";
    }
    if (label == "Collect context") {
        return "list-tree";
    }
    if (label == "Propose patch" || label == "Review ChangeSet") {
        return "git-pull-request";
    }
    if (label == "Run affected tests") {
        return "play";
    }
    return "circle-dot";
}

QColor IconColorForStatus(const QString& status) {
    if (status == "available" || status == "trusted" || status == "ready") {
        return QColor("#2dcec6");
    }
    if (status == "degraded" || status == "restricted" || status == "warn") {
        return QColor("#f0c56a");
    }
    if (status == "unavailable" || status == "untrusted") {
        return QColor("#ff7680");
    }
    return QColor("#9aa6b7");
}

QListWidgetItem* AddStructuredItem(QListWidget& list, const QString& label, const QString& detail, const QString& status) {
    auto* item = new QListWidgetItem(&list);
    item->setSizeHint(QSize(0, 28));

    auto* card = new QFrame(&list);
    card->setObjectName("StructuredItem");
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(4);

    auto* icon = new QLabel(card);
    icon->setObjectName("ItemIcon");
    icon->setPixmap(IconPixmap(IconForLabel(label), IconColorForStatus(status), QSize(15, 15)));
    icon->setFixedSize(18, 18);
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon);

    auto* text = new QLabel(card);
    text->setObjectName("ItemText");
    text->setTextFormat(Qt::RichText);
    text->setText(detail.isEmpty()
        ? label.toHtmlEscaped()
        : QString("<b>%1</b>: %2").arg(label.toHtmlEscaped(), detail.toHtmlEscaped()));
    text->setWordWrap(false);
    text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(text, 1);

    auto* badge = new QLabel(status, card);
    badge->setObjectName(
        status == "available" || status == "trusted" || status == "ready"
            ? "StatusBadgeOk"
            : status == "degraded" || status == "restricted" || status == "warn"
                ? "StatusBadgeWarn"
                : status == "unavailable" || status == "untrusted"
                    ? "StatusBadgeError"
                    : "StatusBadgeIdle");
    badge->setAlignment(Qt::AlignCenter);
    layout->addWidget(badge, 0, Qt::AlignTop);

    list.setItemWidget(item, card);
    return item;
}

void AddCapabilityItem(QListWidget& list, const QString& label, const QString& detail, const QString& status) {
    AddStructuredItem(list, label, detail, status);
}

QString JoinIds(const std::vector<std::string>& ids, int limit = 3) {
    QStringList values;
    for (int i = 0; i < static_cast<int>(ids.size()) && i < limit; ++i) {
        values.push_back(ToQString(ids[static_cast<std::size_t>(i)]));
    }
    if (static_cast<int>(ids.size()) > limit) {
        values.push_back(QString("+%1").arg(static_cast<int>(ids.size()) - limit));
    }
    return values.join(", ");
}

QString CountLabel(std::size_t count, const QString& unit) {
    return QString("%1 %2").arg(static_cast<qulonglong>(count)).arg(unit);
}

QString RegistryTitle(const Capability& capability) {
    return ToQString(capability.title.empty() ? capability.id : capability.title);
}

QString RegistryDetail(const Capability& capability) {
    QStringList lines;
    if (!capability.provider_id.empty()) {
        lines.push_back("Provider: " + ToQString(capability.provider_id));
    }
    if (!capability.message.empty()) {
        lines.push_back(ToQString(capability.message));
    }
    for (const auto& detail : capability.details) {
        lines.push_back(ToQString(detail.first + ": " + detail.second));
    }
    return lines.join("\n");
}

}

InspectorPanel::InspectorPanel(QWidget* parent) : QFrame(parent) {
    setObjectName("InspectorPanel");
    setMinimumWidth(160);
    setMinimumHeight(0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(9);

    layout->addWidget(SectionTitle("Context Stack", this));
    context_list_ = new QListWidget(this);
    context_list_->setMinimumHeight(90);
    context_list_->setMaximumHeight(140);
    context_list_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    context_list_->setWordWrap(true);
    context_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    context_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    context_list_->setSpacing(3);
    connect(context_list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item == nullptr || !diagnostic_activated_handler_) {
            return;
        }
        const int index = item->data(Qt::UserRole).toInt();
        if (index >= 0 && index < static_cast<int>(diagnostics_.size())) {
            diagnostic_activated_handler_(diagnostics_[static_cast<std::size_t>(index)]);
        }
    });
    layout->addWidget(context_list_, 1);

    layout->addWidget(SectionTitle("Capability Lens", this));
    capability_list_ = new QListWidget(this);
    capability_list_->setMinimumHeight(110);
    capability_list_->setMaximumHeight(140);
    capability_list_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    capability_list_->setWordWrap(true);
    capability_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    capability_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    capability_list_->setSpacing(3);
    layout->addWidget(capability_list_, 1);

    layout->addWidget(SectionTitle("Plan", this));
    plan_list_ = new QListWidget(this);
    plan_list_->setMinimumHeight(120);
    plan_list_->setMaximumHeight(175);
    plan_list_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    plan_list_->setWordWrap(true);
    plan_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    plan_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    plan_list_->setSpacing(3);
    AddStructuredItem(*plan_list_, "Inspect failure", "Collect current evidence", "ready");
    AddStructuredItem(*plan_list_, "Collect context", "Files, diagnostics, graph nodes", "ready");
    AddStructuredItem(*plan_list_, "Propose patch", "Awaiting Change Studio", "degraded");
    AddStructuredItem(*plan_list_, "Run affected tests", "Depends on provider", "available");
    AddStructuredItem(*plan_list_, "Review ChangeSet", "Required before apply", "warn");
    layout->addWidget(plan_list_, 2);
}

void InspectorPanel::SetState(const UiState* state, WorkspaceContext* context) {
    context_list_->clear();
    capability_list_->clear();
    diagnostics_.clear();

    if (state == nullptr || !state->workspace_open) {
        AddStructuredItem(*context_list_, "Test", "auth_login_fails", "warn");
        AddStructuredItem(*context_list_, "Symbol", "validate_token", "ready");
        AddStructuredItem(*context_list_, "File", "auth_service.py", "ready");
        AddStructuredItem(*context_list_, "Job", "pytest failed", "warn");
        AddCapabilityItem(*capability_list_, "Run", "Available", "available");
        AddCapabilityItem(*capability_list_, "Tests", "Available", "available");
        AddCapabilityItem(*capability_list_, "Index", "Ready", "ready");
        AddCapabilityItem(*capability_list_, "Agent tools", "Available", "available");
        return;
    }

    AddStructuredItem(*context_list_, "Workspace", ToQString(state->workspace.name), "ready");
    if (!state->tabs.empty()) {
        AddStructuredItem(*context_list_, "File", ToQString(state->tabs.back().title), "ready");
    }
    if (!state->problems.empty()) {
        for (const Diagnostic& diagnostic : state->problems) {
            diagnostics_.push_back(diagnostic);
            auto* item = AddStructuredItem(*context_list_, "Diagnostic", ToQString(diagnostic.message).left(64), "warn");
            item->setData(Qt::UserRole, static_cast<int>(diagnostics_.size() - 1));
        }
    }
    if (!state->jobs.empty()) {
        AddStructuredItem(*context_list_, "Operation", QString("Job #%1").arg(state->jobs.back().id), "ready");
    }

    if (context == nullptr) {
        AddCapabilityItem(*capability_list_, "Core", "Workspace runtime is not connected.", "degraded");
        return;
    }

    const auto capabilities = context->Capabilities().Capabilities();
    for (const Capability& capability : capabilities) {
        AddCapabilityItem(
            *capability_list_,
            RegistryTitle(capability),
            RegistryDetail(capability),
            ToQString(ToString(capability.status)));
    }

    const auto language_ids = context->Languages().LanguageIds();
    AddCapabilityItem(
        *capability_list_,
        "Languages",
        JoinIds(language_ids),
        language_ids.empty() ? "unavailable" : "available");

    const auto build_ids = context->Build().BuildProviderIds();
    AddCapabilityItem(
        *capability_list_,
        "Build providers",
        JoinIds(build_ids),
        build_ids.empty() ? "unavailable" : "available");

    const auto run_ids = context->RunConfigurations().ProviderIds();
    AddCapabilityItem(
        *capability_list_,
        "Run providers",
        JoinIds(run_ids),
        run_ids.empty() ? "unavailable" : "available");

    const auto index_ids = context->Indexes().ProviderIds();
    const auto index_snapshots = context->Indexes().Snapshots();
    const auto ready_count = std::count_if(index_snapshots.begin(), index_snapshots.end(), [](const IndexSnapshot& snapshot) {
        return snapshot.status == IndexStatus::Ready;
    });
    QString index_status = "unavailable";
    if (!index_snapshots.empty()) {
        index_status = ready_count > 0 ? "ready" : "degraded";
    } else if (!index_ids.empty()) {
        index_status = "available";
    }
    AddCapabilityItem(
        *capability_list_,
        "Index graph",
        index_snapshots.empty() ? JoinIds(index_ids) : CountLabel(index_snapshots.size(), "snapshots"),
        index_status);

    const auto tools = context->AgentTools().Tools();
    AddCapabilityItem(
        *capability_list_,
        "Agent tools",
        CountLabel(tools.size(), "tools"),
        tools.empty() ? "unavailable" : "available");

    const WorkspaceTrustLevel trust_level = context->WorkspaceTrust().Level();
    AddCapabilityItem(
        *capability_list_,
        "Workspace trust",
        ToQString(ToString(trust_level)),
        ToQString(ToString(trust_level)));
}

void InspectorPanel::SetDiagnosticActivatedHandler(DiagnosticActivatedHandler handler) {
    diagnostic_activated_handler_ = std::move(handler);
}

}
