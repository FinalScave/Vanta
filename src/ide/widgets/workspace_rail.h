#pragma once

#include <QFrame>

namespace mornox::ide {

class WorkspaceRail final : public QFrame {
public:
    explicit WorkspaceRail(QWidget* parent = nullptr);
};

}
