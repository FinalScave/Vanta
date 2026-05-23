#include "widgets/operation_ledger.h"

#include <QColor>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QSize>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

#include "mornox/execution/job_service.h"
#include "mornox/workspace/ide_event.h"
#include "icons.h"
#include "workspace_controller.h"

namespace mornox::ide {
namespace {

QTableWidgetItem* LedgerCell(
    const QString& text,
    const QColor& color = QColor("#dce5ef"),
    const QString& icon = {},
    const QColor& icon_color = QColor("#9aa6b7")) {
    auto* item = new QTableWidgetItem(text);
    item->setForeground(color);
    item->setBackground(QColor("#10161d"));
    if (!icon.isEmpty()) {
        item->setIcon(Icon(icon, icon_color, QSize(16, 16)));
    }
    return item;
}

QString OperationIcon(const QString& operation) {
    if (operation.contains("test", Qt::CaseInsensitive)) {
        return "flask-conical";
    }
    if (operation.contains("read", Qt::CaseInsensitive)) {
        return "file";
    }
    if (operation.contains("symbol", Qt::CaseInsensitive)) {
        return "code";
    }
    if (operation.contains("ChangeSet", Qt::CaseInsensitive) || operation.contains("patch", Qt::CaseInsensitive)) {
        return "git-pull-request";
    }
    if (operation.contains("approval", Qt::CaseInsensitive)) {
        return "lock-keyhole";
    }
    return "terminal";
}

QString ResultIcon(const QString& result) {
    if (result == "Success") {
        return "circle-check";
    }
    if (result == "Failed") {
        return "circle-x";
    }
    if (result == "Pending") {
        return "lock-keyhole";
    }
    if (result == "Proposed") {
        return "git-pull-request";
    }
    return "circle-dot";
}

QColor ResultColor(const QString& result) {
    if (result == "Failed") {
        return QColor("#ff7680");
    }
    if (result == "Pending" || result == "Proposed") {
        return QColor("#f0c56a");
    }
    return QColor("#bff7f2");
}

void AddLedgerRow(
    QTableWidget& table,
    const QString& time,
    const QString& operation,
    const QString& source,
    const QString& target,
    const QString& result) {
    const int row = table.rowCount();
    table.insertRow(row);
    table.setItem(row, 0, LedgerCell(time, QColor("#9aa6b7")));
    table.setItem(row, 1, LedgerCell(operation, QColor("#dce5ef"), OperationIcon(operation), QColor("#9aa6b7")));
    table.setItem(row, 2, LedgerCell(source, QColor("#aeb8c6")));
    table.setItem(row, 3, LedgerCell(target, QColor("#aeb8c6")));
    table.setItem(row, 4, LedgerCell(result, ResultColor(result), ResultIcon(result), ResultColor(result)));
}

}

OperationLedger::OperationLedger(QWidget* parent) : QFrame(parent) {
    setObjectName("OperationLedger");
    setMinimumHeight(130);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(7);

    auto* header = new QWidget(this);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(8);
    auto* title = new QLabel("Operation Ledger", this);
    title->setObjectName("PanelTitle");
    header_layout->addWidget(title);
    auto* filter = new QLabel("All Operations", this);
    filter->setObjectName("InlinePill");
    header_layout->addWidget(filter);
    header_layout->addStretch(1);
    auto* summary = new QLabel("1 failed / 5 successful / 1 pending", this);
    summary->setObjectName("PanelMeta");
    header_layout->addWidget(summary);
    layout->addWidget(header);

    auto* content = new QSplitter(Qt::Horizontal, this);
    content->setChildrenCollapsible(false);

    table_ = new QTableWidget(0, 5, content);
    table_->setObjectName("LedgerTable");
    table_->setHorizontalHeaderLabels({"Time", "Operation", "Source", "Target", "Result"});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(23);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const auto found = row_jobs_.find(row);
        if (found != row_jobs_.end() && job_activated_handler_) {
            job_activated_handler_(found->second);
        }
    });
    content->addWidget(table_);

    detail_ = new QLabel(content);
    detail_->setObjectName("LedgerDetail");
    detail_->setTextFormat(Qt::RichText);
    detail_->setWordWrap(true);
    detail_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detail_->setText(
        "<span style='color:#f0f4fb'><b>run pytest</b></span>"
        " &nbsp; <span style='color:#8f9bac'>Agent / 10:21:20</span><br><br>"
        "<span style='color:#8f9bac'>Command</span><br>"
        "pytest -k auth_login_fails -q<br><br>"
        "<span style='color:#8f9bac'>Result</span><br>"
        "<span style='color:#ff7680'>Exit code 1</span> / 1 failed, 41 passed in 2.31s<br><br>"
        "<span style='color:#8f9bac'>Outputs</span><br>"
        "pytest.log &nbsp;&nbsp; junit.xml");
    content->addWidget(detail_);
    content->setStretchFactor(0, 3);
    content->setStretchFactor(1, 2);
    layout->addWidget(content);
}

void OperationLedger::SetState(const UiState* state) {
    table_->setRowCount(0);
    row_jobs_.clear();
    if (state == nullptr || !state->workspace_open) {
        AddLedgerRow(*table_, "10:21:17", "read file", "Agent", "auth_service.py", "Success");
        AddLedgerRow(*table_, "10:21:18", "query symbols", "Agent", "validate_token", "Success");
        AddLedgerRow(*table_, "10:21:20", "run pytest", "Agent", "tests/test_auth.py::test_auth_login_fails", "Failed");
        AddLedgerRow(*table_, "10:21:24", "propose ChangeSet", "Agent", "2 files", "Proposed");
        AddLedgerRow(*table_, "10:21:25", "approval requested", "Agent", "ChangeSet #15", "Pending");
        AddLedgerRow(*table_, "10:21:32", "tests passed", "Agent", "pytest -k auth_login_fails", "Success");
        table_->selectRow(2);
        return;
    }

    if (state->last_event) {
        AddLedgerRow(
            *table_,
            "--",
            ToQString(ToString(state->last_event->kind)),
            ToQString(state->last_event->source),
            ToQString(state->last_event->message),
            "Event");
    }

    for (const JobRecord& job : state->jobs) {
        const int row = table_->rowCount();
        AddLedgerRow(*table_, "--", "job", QString("#%1").arg(job.id), ToQString(job.title), ToQString(ToString(job.status)));
        row_jobs_[row] = job.id;
    }
    if (table_->rowCount() > 0) {
        table_->selectRow(table_->rowCount() - 1);
    }
}

void OperationLedger::SetJobActivatedHandler(JobActivatedHandler handler) {
    job_activated_handler_ = std::move(handler);
}

}
