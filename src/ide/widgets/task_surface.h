#pragma once

#include <functional>
#include <map>
#include <optional>

#include <QWidget>

#include "ui/ui_state_store.h"

class QLabel;
class QPlainTextEdit;
class QTabWidget;

namespace mornox::ide {

class TaskSurface final : public QWidget {
public:
    using CloseRequestHandler = std::function<bool(const QString& key, const QString& title, bool dirty)>;

    explicit TaskSurface(QWidget* parent = nullptr);

    void SetState(const UiState* state);
    void OpenTextFile(const QString& key, const QString& title, const QString& text);
    void OpenJobResult(const JobRecord& job);
    void RevealLocation(const QString& key, int line, int column);
    std::optional<QString> CurrentFileKey() const;
    QString CurrentText() const;
    std::optional<QString> TextForKey(const QString& key) const;
    bool CurrentFileDirty() const;
    bool HasDirtyEditors() const;
    bool ConfirmAllClosable(const CloseRequestHandler& handler) const;
    void MarkSaved(const QString& key);
    void SetCloseRequestHandler(CloseRequestHandler handler);

private:
    struct EditorEntry {
        QPlainTextEdit* editor = nullptr;
        QString title;
        bool dirty = false;
        bool loading = false;
    };

    void UpdateTabTitle(const QString& key);
    QString KeyForWidget(QWidget* widget) const;

    QTabWidget* tabs_ = nullptr;
    QLabel* surface_subtitle_ = nullptr;
    QLabel* test_summary_ = nullptr;
    QLabel* failure_card_ = nullptr;
    QLabel* annotation_card_ = nullptr;
    QPlainTextEdit* test_preview_ = nullptr;
    QPlainTextEdit* code_preview_ = nullptr;
    std::map<QString, EditorEntry> editors_;
    CloseRequestHandler close_request_handler_;
};

}
