#pragma once

#include <functional>
#include <vector>

#include <QFrame>

#include "ui/ui_state_store.h"

class QListWidget;

namespace mornox {
class WorkspaceContext;
}

namespace mornox::ide {

class InspectorPanel final : public QFrame {
public:
    using DiagnosticActivatedHandler = std::function<void(const Diagnostic&)>;

    explicit InspectorPanel(QWidget* parent = nullptr);

    void SetState(const UiState* state, WorkspaceContext* context = nullptr);
    void SetDiagnosticActivatedHandler(DiagnosticActivatedHandler handler);

private:
    QListWidget* context_list_ = nullptr;
    QListWidget* capability_list_ = nullptr;
    QListWidget* plan_list_ = nullptr;
    std::vector<Diagnostic> diagnostics_;
    DiagnosticActivatedHandler diagnostic_activated_handler_;
};

}
