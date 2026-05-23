#pragma once

#include <QString>
#include <QWidget>
#include <QVector>

#include "ui/ui_state_store.h"

namespace mornox::ide {

class MissionMapPanel final : public QWidget {
public:
    explicit MissionMapPanel(QWidget* parent = nullptr);

    void SetState(const UiState* state);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Node {
        QString label;
        QString role;
        QString status;
        QPointF center;
    };

    QVector<Node> NodesForSize() const;
    QString diagnostic_label_ = "auth_login_fails";
    QString file_label_ = "auth_service.py";
    QString job_label_ = "job #42";
};

}
