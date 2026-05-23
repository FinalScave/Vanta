#pragma once

#include <functional>
#include <map>

#include <QFrame>

#include "ui/ui_state_store.h"

class QTableWidget;
class QLabel;

namespace mornox::ide {

class OperationLedger final : public QFrame {
public:
    using JobActivatedHandler = std::function<void(JobId)>;

    explicit OperationLedger(QWidget* parent = nullptr);

    void SetState(const UiState* state);
    void SetJobActivatedHandler(JobActivatedHandler handler);

private:
    QTableWidget* table_ = nullptr;
    QLabel* detail_ = nullptr;
    std::map<int, JobId> row_jobs_;
    JobActivatedHandler job_activated_handler_;
};

}
