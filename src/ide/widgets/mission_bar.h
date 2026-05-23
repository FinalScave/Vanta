#pragma once

#include <QFrame>

class QLineEdit;
class QPushButton;

namespace mornox::ide {

class MissionBar final : public QFrame {
public:
    explicit MissionBar(QWidget* parent = nullptr);

    QLineEdit* Input() const;
    void SetMissionText(const QString& text);

private:
    QPushButton* mission_label_ = nullptr;
    QLineEdit* input_ = nullptr;
};

}
