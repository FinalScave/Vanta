#include "widgets/task_surface.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSizePolicy>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextFormat>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "icons.h"
#include "workspace_controller.h"

namespace mornox::ide {
namespace {

QLabel* CompactCard(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("MetricLabel");
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return label;
}

QPlainTextEdit* ReadOnlyText(const QString& text, QWidget* parent) {
    auto* edit = new QPlainTextEdit(parent);
    QFont font("Cascadia Mono");
    font.setStyleHint(QFont::Monospace);
    edit->setFont(font);
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    edit->setMinimumSize(80, 90);
    edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    edit->setPlainText(text);
    return edit;
}

QString PreviewCode() {
    return R"(def validate_token(token: str) -> bool:
    """validate user token."""
    if not token:
        return False

    try:
        payload = decode_jwt(token)
    except Exception:
        return False

    # token must have user_id and not expired
    if "user_id" not in payload:
        return False

    if is_expired(payload.get("exp")):
        return False

    user = load_user(payload["user_id"])
    if not user or not user.active:
        return False

    return True
)";
}

void HighlightPreviewLine(QPlainTextEdit& edit, const QString& needle) {
    QList<QTextEdit::ExtraSelection> selections;
    QTextCursor found = edit.document()->find(needle);
    if (!found.isNull()) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(found.block());
        selection.cursor.clearSelection();
        selection.format.setBackground(QColor("#3a1f24"));
        selection.format.setForeground(QColor("#f7d6da"));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selections.push_back(selection);
    }
    edit.setExtraSelections(selections);
}

void DisableTabClose(QTabWidget& tabs, int index) {
    tabs.tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
}

}

TaskSurface::TaskSurface(QWidget* parent) : QWidget(parent) {
    setObjectName("TaskSurface");
    setMinimumSize(240, 220);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    tabs_ = new QTabWidget(this);
    tabs_->setMinimumWidth(0);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    layout->addWidget(tabs_, 1);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget* widget = tabs_->widget(index);
        const QString key = KeyForWidget(widget);
        if (key.isEmpty()) {
            return;
        }
        auto it = editors_.find(key);
        if (it == editors_.end()) {
            return;
        }
        if (close_request_handler_ && !close_request_handler_(key, it->second.title, it->second.dirty)) {
            return;
        }
        editors_.erase(it);
        tabs_->removeTab(index);
        delete widget;
    });

    auto* editor_home = new QWidget(tabs_);
    auto* editor_layout = new QVBoxLayout(editor_home);
    editor_layout->setContentsMargins(12, 12, 12, 12);
    editor_layout->addWidget(CompactCard(
        "<b>Editor</b><br>Open a file from Files perspective or a diagnostic context item.",
        editor_home));
    editor_layout->addStretch(1);
    int fixed_index = tabs_->addTab(editor_home, "Editor");
    tabs_->setTabIcon(fixed_index, Icon("file-code", QColor("#9aa6b7"), QSize(16, 16)));
    DisableTabClose(*tabs_, fixed_index);

    auto* test_page = new QWidget(tabs_);
    auto* test_layout = new QHBoxLayout(test_page);
    test_layout->setContentsMargins(8, 8, 8, 8);
    test_layout->setSpacing(8);

    auto* splitter = new QSplitter(Qt::Horizontal, test_page);
    splitter->setChildrenCollapsible(false);

    auto* result_panel = new QWidget(splitter);
    auto* result_layout = new QVBoxLayout(result_panel);
    result_layout->setContentsMargins(0, 0, 0, 0);
    result_layout->setSpacing(8);
    test_summary_ = CompactCard("No active test result.", result_panel);
    test_summary_->setObjectName("TestSummary");
    test_summary_->setMinimumWidth(140);
    result_layout->addWidget(test_summary_);
    failure_card_ = CompactCard("No selected failure.", result_panel);
    failure_card_->setObjectName("FailureCard");
    result_layout->addWidget(failure_card_);
    test_preview_ = ReadOnlyText("Operation output appears here.", result_panel);
    test_preview_->setObjectName("OutputBlock");
    result_layout->addWidget(test_preview_, 1);
    splitter->addWidget(result_panel);

    auto* editor_panel = new QWidget(splitter);
    auto* editor_panel_layout = new QVBoxLayout(editor_panel);
    editor_panel_layout->setContentsMargins(0, 0, 0, 0);
    editor_panel_layout->setSpacing(8);
    surface_subtitle_ = CompactCard("<b>auth_service.py</b><br>Python", editor_panel);
    surface_subtitle_->setObjectName("SourceHeader");
    editor_panel_layout->addWidget(surface_subtitle_);

    auto* code_area = new QWidget(editor_panel);
    auto* code_area_layout = new QHBoxLayout(code_area);
    code_area_layout->setContentsMargins(0, 0, 0, 0);
    code_area_layout->setSpacing(8);
    code_preview_ = ReadOnlyText(PreviewCode(), editor_panel);
    code_preview_->setObjectName("CodePreview");
    code_preview_->setMinimumWidth(120);
    code_area_layout->addWidget(code_preview_, 3);
    annotation_card_ = CompactCard(
        "<b>Assertion failed here</b><br>"
        "<span style='color:#91a0b0'>Expected True, got False</span><br><br>"
        "<span style='color:#bff7f2'>Open linked test</span>",
        code_area);
    annotation_card_->setObjectName("AnnotationCard");
    annotation_card_->setMinimumWidth(90);
    code_area_layout->addWidget(annotation_card_, 1);
    editor_panel_layout->addWidget(code_area, 1);
    splitter->addWidget(editor_panel);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({260, 360});

    test_layout->addWidget(splitter);
    fixed_index = tabs_->addTab(test_page, "Test Result");
    tabs_->setTabIcon(fixed_index, Icon("circle-x", QColor("#ff7680"), QSize(16, 16)));
    DisableTabClose(*tabs_, fixed_index);

    auto* graph_page = new QWidget(tabs_);
    auto* graph_layout = new QVBoxLayout(graph_page);
    graph_layout->setContentsMargins(12, 12, 12, 12);
    graph_layout->addWidget(CompactCard(
        "<b>Project Graph</b><br>Open the full mission graph from the left Graph perspective.",
        graph_page));
    graph_layout->addStretch(1);
    fixed_index = tabs_->addTab(graph_page, "Graph");
    tabs_->setTabIcon(fixed_index, Icon("network", QColor("#9aa6b7"), QSize(16, 16)));
    DisableTabClose(*tabs_, fixed_index);

    auto* change_page = new QWidget(tabs_);
    auto* change_layout = new QVBoxLayout(change_page);
    change_layout->setContentsMargins(12, 12, 12, 12);
    change_layout->addWidget(CompactCard(
        "<b>Change Studio</b><br>Agent and refactoring edits should arrive here as reviewable ChangeSets.",
        change_page));
    change_layout->addStretch(1);
    fixed_index = tabs_->addTab(change_page, "Change Studio");
    tabs_->setTabIcon(fixed_index, Icon("git-pull-request", QColor("#f0c56a"), QSize(16, 16)));
    DisableTabClose(*tabs_, fixed_index);

    tabs_->setCurrentIndex(1);
}

void TaskSurface::SetState(const UiState* state) {
    if (state == nullptr || !state->workspace_open) {
        test_summary_->setText(
            "<b>auth_login_fails</b><br>"
            "<span style='color:#91a0b0'>tests/test_auth.py::test_auth_login_fails</span><br><br>"
            "<span style='color:#ff7680'><b>1</b></span>/42 passed &nbsp;&nbsp; <b>2.31s</b>"
            "&nbsp;&nbsp; <span style='color:#f0c56a'>Pytest</span>");
        failure_card_->setText(
            "<b>Failure</b><br>"
            "<span style='color:#f0c56a'>test_auth_login_fails</span><br>"
            "<span style='color:#91a0b0'>tests/test_auth.py:42</span>");
        test_preview_->setPlainText(R"(38  E    AssertionError:
39  E      assert False
40  E      + where False =
41  E        <function validate_token at
42  >        0x0000023A5B1F3D40>("invalid"))");
        surface_subtitle_->setText("<b>auth_service.py</b><br>Python");
        code_preview_->setPlainText(PreviewCode());
        annotation_card_->setText(
            "<b>Assertion failed here</b><br>"
            "<span style='color:#91a0b0'>Expected True, got False</span><br><br>"
            "<span style='color:#bff7f2'>Open linked test</span>");
        HighlightPreviewLine(*code_preview_, "if is_expired");
        return;
    }

    QString title = "Active mission";
    QString diagnostic_text = "No diagnostics";
    if (!state->problems.empty()) {
        diagnostic_text = ToQString(state->problems.front().message);
        title = ToQString(state->problems.front().location.file.DisplayName());
    } else if (!state->tabs.empty()) {
        title = ToQString(state->tabs.back().title);
    }

    test_summary_->setText(QString(
        "<b>%1</b><br>"
        "<span style='color:#91a0b0'>Mission evidence</span><br><br>"
        "<b>%2</b> problems &nbsp;&nbsp; <b>%3</b> jobs<br><br>"
        "<b>Current diagnostic</b><br>%4")
        .arg(title.toHtmlEscaped())
        .arg(static_cast<qulonglong>(state->problems.size()))
        .arg(static_cast<qulonglong>(state->jobs.size()))
        .arg(diagnostic_text.toHtmlEscaped()));
    failure_card_->setText(QString(
        "<b>Focused evidence</b><br>"
        "<span style='color:#f0c56a'>%1</span><br>"
        "<span style='color:#91a0b0'>Double-click a context item to reveal source</span>")
        .arg(diagnostic_text.toHtmlEscaped()));

    QString output = "No operation output yet.";
    if (!state->jobs.empty()) {
        const JobRecord& job = state->jobs.back();
        output = ToQString(job.output.empty() ? job.message : job.output);
    }
    test_preview_->setPlainText(output);
    surface_subtitle_->setText(QString("<b>%1</b><br>Source preview").arg(title.toHtmlEscaped()));
    annotation_card_->setText(
        "<b>Context annotation</b><br>"
        "<span style='color:#91a0b0'>Open files and diagnostics to pin exact lines here.</span>");
    HighlightPreviewLine(*code_preview_, "if is_expired");
}

void TaskSurface::OpenTextFile(const QString& key, const QString& title, const QString& text) {
    auto it = editors_.find(key);
    if (it != editors_.end()) {
        it->second.loading = true;
        it->second.editor->setPlainText(text);
        it->second.loading = false;
        it->second.dirty = false;
        UpdateTabTitle(key);
        tabs_->setCurrentWidget(it->second.editor);
        return;
    }

    auto* editor = new QPlainTextEdit(tabs_);
    QFont editor_font("Cascadia Mono");
    editor_font.setStyleHint(QFont::Monospace);
    editor->setFont(editor_font);
    editor->setPlainText(text);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editors_[key] = {
        .editor = editor,
        .title = title,
    };
    connect(editor, &QPlainTextEdit::textChanged, this, [this, key] {
        auto found = editors_.find(key);
        if (found == editors_.end() || found->second.loading) {
            return;
        }
        found->second.dirty = true;
        UpdateTabTitle(key);
    });
    tabs_->addTab(editor, title);
    tabs_->setTabIcon(tabs_->indexOf(editor), Icon("file-code", QColor("#9aa6b7"), QSize(16, 16)));
    tabs_->setCurrentWidget(editor);
}

void TaskSurface::OpenJobResult(const JobRecord& job) {
    const QString key = QString("job:%1").arg(job.id);
    for (int index = 0; index < tabs_->count(); ++index) {
        QWidget* widget = tabs_->widget(index);
        if (widget->property("mornoxKey").toString() == key) {
            tabs_->setCurrentWidget(widget);
            auto* editor = widget->findChild<QPlainTextEdit*>("jobOutput");
            if (editor != nullptr) {
                editor->setPlainText(ToQString(job.output.empty() ? job.message : job.output));
            }
            return;
        }
    }

    auto* page = new QWidget(tabs_);
    page->setProperty("mornoxKey", key);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* title = CompactCard(QString("<b>%1</b><br>Status: %2<br>Message: %3")
                                  .arg(ToQString(job.title).toHtmlEscaped())
                                  .arg(ToQString(ToString(job.status)).toHtmlEscaped())
                                  .arg(ToQString(job.message).toHtmlEscaped()), page);
    layout->addWidget(title);

    auto* output = ReadOnlyText(ToQString(job.output.empty() ? job.message : job.output), page);
    output->setObjectName("jobOutput");
    layout->addWidget(output, 1);

    tabs_->addTab(page, QString("Job #%1").arg(job.id));
    tabs_->setTabIcon(tabs_->indexOf(page), Icon("terminal", QColor("#9aa6b7"), QSize(16, 16)));
    tabs_->setCurrentWidget(page);
}

void TaskSurface::RevealLocation(const QString& key, int line, int column) {
    auto it = editors_.find(key);
    if (it == editors_.end()) {
        return;
    }
    QPlainTextEdit* editor = it->second.editor;
    tabs_->setCurrentWidget(editor);

    const int line_index = line > 0 ? line - 1 : 0;
    const QTextBlock block = editor->document()->findBlockByNumber(line_index);
    if (block.isValid()) {
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, std::max(0, column));
        editor->setTextCursor(cursor);
        editor->centerCursor();
    }

    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection selection;
    selection.cursor = QTextCursor(block);
    selection.cursor.clearSelection();
    selection.format.setBackground(QColor("#3b2f13"));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selections.push_back(selection);
    editor->setExtraSelections(selections);
}

std::optional<QString> TaskSurface::CurrentFileKey() const {
    const QString key = KeyForWidget(tabs_->currentWidget());
    return key.isEmpty() ? std::nullopt : std::optional<QString>(key);
}

QString TaskSurface::CurrentText() const {
    const QString key = KeyForWidget(tabs_->currentWidget());
    const auto text = TextForKey(key);
    return text.value_or(QString());
}

std::optional<QString> TaskSurface::TextForKey(const QString& key) const {
    auto it = editors_.find(key);
    if (it == editors_.end()) {
        return std::nullopt;
    }
    return it->second.editor->toPlainText();
}

bool TaskSurface::CurrentFileDirty() const {
    const QString key = KeyForWidget(tabs_->currentWidget());
    auto it = editors_.find(key);
    return it != editors_.end() && it->second.dirty;
}

bool TaskSurface::HasDirtyEditors() const {
    for (const auto& [key, entry] : editors_) {
        (void)key;
        if (entry.dirty) {
            return true;
        }
    }
    return false;
}

bool TaskSurface::ConfirmAllClosable(const CloseRequestHandler& handler) const {
    for (const auto& [key, entry] : editors_) {
        if (entry.dirty && handler && !handler(key, entry.title, true)) {
            return false;
        }
    }
    return true;
}

void TaskSurface::MarkSaved(const QString& key) {
    auto it = editors_.find(key);
    if (it == editors_.end()) {
        return;
    }
    it->second.dirty = false;
    UpdateTabTitle(key);
}

void TaskSurface::SetCloseRequestHandler(CloseRequestHandler handler) {
    close_request_handler_ = std::move(handler);
}

void TaskSurface::UpdateTabTitle(const QString& key) {
    auto it = editors_.find(key);
    if (it == editors_.end()) {
        return;
    }
    const int index = tabs_->indexOf(it->second.editor);
    if (index >= 0) {
        tabs_->setTabText(index, it->second.dirty ? "*" + it->second.title : it->second.title);
    }
}

QString TaskSurface::KeyForWidget(QWidget* widget) const {
    for (const auto& [key, entry] : editors_) {
        if (entry.editor == widget) {
            return key;
        }
    }
    return {};
}

}
