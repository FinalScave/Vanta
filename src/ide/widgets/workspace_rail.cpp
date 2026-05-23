#include "widgets/workspace_rail.h"

#include <QColor>
#include <QList>
#include <QPair>
#include <QSizePolicy>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

#include "icons.h"

namespace mornox::ide {
namespace {

QToolButton* RailButton(const QString& label, const QString& icon, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setObjectName("RailButton");
    button->setText(label);
    button->setIcon(Icon(icon, QColor("#9aa6b7"), QSize(21, 21)));
    button->setIconSize(QSize(21, 21));
    button->setToolButtonStyle(label.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
    button->setCheckable(!label.isEmpty());
    button->setMinimumSize(label.isEmpty() ? QSize(42, 34) : QSize(58, 52));
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setToolTip(label.isEmpty() ? icon : label);
    return button;
}

}

WorkspaceRail::WorkspaceRail(QWidget* parent) : QFrame(parent) {
    setObjectName("WorkspaceRail");
    setMinimumWidth(70);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 10, 8, 10);
    layout->setSpacing(7);

    const QList<QPair<QString, QString>> entries = {
        {"Graph", "network"},
        {"Files", "file"},
        {"Symbols", "code"},
        {"Targets", "target"},
        {"Tests", "flask-conical"},
        {"Changes", "git-pull-request"},
        {"Agent", "bot"},
    };
    for (const auto& entry : entries) {
        auto* button = RailButton(entry.first, entry.second, this);
        if (entry.first == "Graph") {
            button->setChecked(true);
        }
        layout->addWidget(button);
    }
    layout->addStretch(1);
    layout->addWidget(RailButton("", "chevron-down", this));
    layout->addWidget(RailButton("", "settings-2", this));
}

}
