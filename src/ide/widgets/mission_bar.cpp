#include "widgets/mission_bar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPair>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

#include "icons.h"

namespace mornox::ide {
namespace {

QPushButton* IconButton(const QString& text, const QString& icon, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName("CommandButton");
    button->setIcon(Icon(icon, QColor("#dce5ef"), QSize(17, 17)));
    button->setIconSize(QSize(17, 17));
    button->setMinimumHeight(34);
    button->setMinimumWidth(text.isEmpty() ? 36 : 76);
    return button;
}

}

MissionBar::MissionBar(QWidget* parent) : QFrame(parent) {
    setObjectName("MissionBar");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(12);

    auto* brand = new QWidget(this);
    auto* brand_layout = new QHBoxLayout(brand);
    brand_layout->setContentsMargins(0, 0, 0, 0);
    brand_layout->setSpacing(8);
    auto* logo = new QLabel(brand);
    logo->setObjectName("BrandIcon");
    logo->setPixmap(IconPixmap("box", QColor("#2dcec6"), QSize(24, 24)));
    brand_layout->addWidget(logo);
    auto* brand_text = new QWidget(brand);
    auto* brand_text_layout = new QVBoxLayout(brand_text);
    brand_text_layout->setContentsMargins(0, 0, 0, 0);
    brand_text_layout->setSpacing(1);
    auto* title = new QLabel("Mornox", brand_text);
    title->setObjectName("AppTitle");
    auto* product = new QLabel("MISSION WORKBENCH", brand_text);
    product->setObjectName("MissionEyebrow");
    brand_text_layout->addWidget(title);
    brand_text_layout->addWidget(product);
    brand_layout->addWidget(brand_text);
    layout->addWidget(brand);

    mission_label_ = IconButton("Fix failing tests", "route", this);
    mission_label_->setObjectName("MissionPill");
    mission_label_->setMinimumWidth(180);
    layout->addWidget(mission_label_);

    input_ = new QLineEdit(this);
    input_->setPlaceholderText("Describe an intent, command, search, or agent action...");
    input_->setMinimumHeight(36);
    input_->addAction(Icon("search", QColor("#8f9bac"), QSize(17, 17)), QLineEdit::LeadingPosition);
    layout->addWidget(input_, 1);

    const QList<QPair<QString, QString>> commands = {
        {"Run", "play"},
        {"Test", "flask-conical"},
        {"Review", "eye"},
        {"Commit", "git-branch"},
    };
    for (const auto& command : commands) {
        layout->addWidget(IconButton(command.first, command.second, this));
    }

    layout->addWidget(IconButton("", "settings-2", this));

    const QList<QPair<QString, QString>> window_controls = {
        {"minimize", "minus"},
        {"maximize", "square"},
        {"close", "x"},
    };
    for (const auto& control : window_controls) {
        auto* button = IconButton("", control.second, this);
        button->setObjectName("WindowButton");
        button->setMinimumSize(34, 34);
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, control] {
            QWidget* top = window();
            if (top == nullptr) {
                return;
            }
            if (control.first == "minimize") {
                top->showMinimized();
            } else if (control.first == "maximize") {
                top->isMaximized() ? top->showNormal() : top->showMaximized();
            } else {
                top->close();
            }
        });
    }
}

QLineEdit* MissionBar::Input() const {
    return input_;
}

void MissionBar::SetMissionText(const QString& text) {
    mission_label_->setText(text.isEmpty() ? "No mission" : text);
}

}
