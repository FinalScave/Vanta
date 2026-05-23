#include "widgets/mission_map_panel.h"

#include <QFontMetrics>
#include <QLineF>
#include <QLinearGradient>
#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPolygonF>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

#include "icons.h"
#include "workspace_controller.h"

namespace mornox::ide {
namespace {

struct Edge {
    int from = 0;
    int to = 0;
    QString label;
    QColor color;
    bool dashed = false;
};

QString IconForRole(const QString& role) {
    if (role == "TEST") {
        return "flask-conical";
    }
    if (role == "FILE") {
        return "file-code";
    }
    if (role == "SYMBOL") {
        return "code";
    }
    if (role == "TARGET") {
        return "target";
    }
    if (role == "JOB") {
        return "terminal";
    }
    if (role == "CHANGE") {
        return "git-pull-request";
    }
    return "network";
}

void DrawEdge(QPainter& painter, const QPointF& from, const QPointF& to, const Edge& edge) {
    QPainterPath path;
    path.moveTo(from);
    const qreal control_y = (from.y() + to.y()) / 2.0;
    path.cubicTo(QPointF(from.x(), control_y), QPointF(to.x(), control_y), to);

    QPen pen(edge.color, 1.35);
    if (edge.dashed) {
        pen.setStyle(Qt::DashLine);
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    QLineF direction(from, to);
    const double angle = std::atan2(-direction.dy(), direction.dx());
    const QPointF arrow_a = to - QPointF(std::cos(angle + 0.55) * 7.0, -std::sin(angle + 0.55) * 7.0);
    const QPointF arrow_b = to - QPointF(std::cos(angle - 0.55) * 7.0, -std::sin(angle - 0.55) * 7.0);
    painter.setBrush(edge.color);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(QPolygonF({to, arrow_a, arrow_b}));

    if (!edge.label.isEmpty()) {
        const QPointF label_pos((from.x() + to.x()) / 2.0, (from.y() + to.y()) / 2.0 - 6.0);
        QRectF label_box(label_pos.x() - 28, label_pos.y() - 9, 56, 18);
        painter.setBrush(QColor("#0d1218"));
        painter.setPen(QPen(QColor("#26313c"), 1));
        painter.drawRoundedRect(label_box, 5, 5);
        painter.setPen(QColor("#8f9bac"));
        painter.drawText(label_box, Qt::AlignCenter, edge.label);
    }
}

}

MissionMapPanel::MissionMapPanel(QWidget* parent) : QWidget(parent) {
    setMinimumSize(170, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void MissionMapPanel::SetState(const UiState* state) {
    if (state == nullptr || !state->workspace_open) {
        diagnostic_label_ = "auth_login_fails";
        file_label_ = "auth_service.py";
        job_label_ = "job #42";
    } else {
        diagnostic_label_ = "No diagnostics";
        file_label_ = state->workspace.name.empty() ? "Workspace" : ToQString(state->workspace.name);
        job_label_ = state->jobs.empty() ? "No jobs" : QString("job #%1").arg(state->jobs.back().id);
        if (!state->problems.empty()) {
            diagnostic_label_ = ToQString(state->problems.front().message);
            file_label_ = ToQString(state->problems.front().location.file.DisplayName());
        }
    }
    update();
}

void MissionMapPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF panel = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient background(panel.topLeft(), panel.bottomRight());
    background.setColorAt(0.0, QColor("#101820"));
    background.setColorAt(0.6, QColor("#0c1015"));
    background.setColorAt(1.0, QColor("#0a0c10"));
    painter.setBrush(background);
    painter.setPen(QPen(QColor("#26313c"), 1));
    painter.drawRoundedRect(panel, 9, 9);

    painter.setPen(QPen(QColor(55, 67, 80, 90), 1));
    for (int x = 20; x < width() - 20; x += 14) {
        for (int y = 68; y < height() - 54; y += 14) {
            painter.drawPoint(x, y);
        }
    }

    painter.setPen(QColor("#f0f4fb"));
    QFont title_font = painter.font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() + 1);
    painter.setFont(title_font);
    painter.drawText(QRectF(14, 10, width() - 28, 22), Qt::AlignLeft | Qt::AlignVCenter, "Mission Graph");

    QFont meta_font = painter.font();
    meta_font.setBold(false);
    meta_font.setPointSize(std::max(8, meta_font.pointSize() - 2));
    painter.setFont(meta_font);
    painter.setPen(QColor("#7f8d9d"));
    painter.drawText(QRectF(14, 30, width() - 28, 18), Qt::AlignLeft | Qt::AlignVCenter, "Live context map");
    painter.setPen(QPen(QColor("#1f2832"), 1));
    painter.drawLine(QPointF(12, 54), QPointF(width() - 12, 54));

    const QVector<Node> nodes = NodesForSize();
    const QVector<Edge> edges = {
        {1, 0, "", QColor("#59464a"), false},
        {2, 0, "", QColor("#3d4d63"), false},
        {3, 0, "", QColor("#31515a"), false},
        {4, 0, "", QColor("#3d4d63"), false},
        {0, 5, "patch", QColor("#2d6870"), false},
        {0, 6, "verify", QColor("#31515a"), false},
        {5, 6, "audit", QColor("#2dcec6"), true},
    };
    for (const Edge& edge : edges) {
        DrawEdge(painter, nodes[edge.from].center, nodes[edge.to].center, edge);
    }

    for (int i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        const QSizeF size(
            i == 0 ? std::min(132.0, width() * 0.52) : std::min(108.0, width() * 0.42),
            i == 0 ? 44.0 : 38.0);
        QRectF box(node.center.x() - size.width() / 2.0, node.center.y() - size.height() / 2.0, size.width(), size.height());
        if (i == 0 || i == 1) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(i == 0 ? QColor(45, 206, 198, 120) : QColor(255, 118, 128, 120), 3));
            painter.drawRoundedRect(box.adjusted(-3, -3, 3, 3), 10, 10);
        }
        painter.setBrush(i == 0 ? QColor("#102c2e") : QColor("#141922"));
        painter.setPen(QPen(i == 0 ? QColor("#2dcec6") : QColor("#303b47"), 1));
        painter.drawRoundedRect(box, 8, 8);

        painter.setBrush(node.status == "warn" ? QColor("#ff7680") : i == 0 ? QColor("#2dcec6") : QColor("#5ba4f0"));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(box.left(), box.top() + 8, 4, box.height() - 16), 2, 2);

        const QColor icon_color = node.status == "warn" ? QColor("#ff7680") : i == 0 ? QColor("#2dcec6") : QColor("#9aa6b7");
        painter.drawPixmap(QRectF(box.left() + 9, box.top() + 9, 15, 15), IconPixmap(IconForRole(node.role), icon_color, QSize(15, 15)), QRectF(0, 0, 15, 15));

        QFont role_font = painter.font();
        role_font.setBold(true);
        role_font.setPointSize(std::max(8, role_font.pointSize()));
        painter.setFont(role_font);
        painter.setPen(i == 0 ? QColor("#bff7f2") : QColor("#9ed7d5"));
        painter.drawText(box.adjusted(29, 5, -9, -22), Qt::AlignLeft | Qt::AlignVCenter, node.role);

        QFont label_font = painter.font();
        label_font.setBold(false);
        label_font.setPointSize(std::max(8, label_font.pointSize() - 1));
        painter.setFont(label_font);
        painter.setPen(QColor("#dbe5ef"));
        const QString label = QFontMetrics(label_font).elidedText(node.label, Qt::ElideRight, static_cast<int>(box.width() - 38));
        painter.drawText(box.adjusted(29, 23, -9, -5), Qt::AlignLeft | Qt::AlignVCenter, label);

        const QRectF status_box(box.right() - 35, box.top() + 6, 24, 8);
        painter.setBrush(node.status == "ready" ? QColor("#2dcec6") : node.status == "warn" ? QColor("#f0c56a") : QColor("#586475"));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(status_box, 4, 4);
    }

    const QRectF tools(18, height() - 58, 104, 28);
    painter.setBrush(QColor("#10161d"));
    painter.setPen(QPen(QColor("#27313d"), 1));
    painter.drawRoundedRect(tools, 7, 7);
    painter.setPen(QColor("#8f9bac"));
    painter.drawText(tools, Qt::AlignCenter, "R   FIT   +");

    const QStringList legend = {"Test", "File", "Symbol", "Target", "Job", "Change"};
    const QList<QColor> colors = {
        QColor("#ff6b5f"),
        QColor("#5ba4f0"),
        QColor("#9a6ee8"),
        QColor("#71a968"),
        QColor("#4b70c8"),
        QColor("#d7973d"),
    };
    qreal x = 18;
    const qreal y = height() - 22;
    QFont legend_font = painter.font();
    legend_font.setPointSize(std::max(7, legend_font.pointSize() - 2));
    painter.setFont(legend_font);
    for (int i = 0; i < legend.size(); ++i) {
        painter.setBrush(colors[i]);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(x + 4, y - 4), 4, 4);
        painter.setPen(QColor("#8f9bac"));
        painter.drawText(QRectF(x + 12, y - 12, 50, 18), Qt::AlignLeft | Qt::AlignVCenter, legend[i]);
        x += 58;
        if (x > width() - 64) {
            break;
        }
    }
}

QVector<MissionMapPanel::Node> MissionMapPanel::NodesForSize() const {
    const double width_value = width();
    const double height_value = height();
    return {
        {"Fix failing tests", "MISSION", "ready", {width_value * 0.50, height_value * 0.43}},
        {diagnostic_label_, "TEST", diagnostic_label_ == "No diagnostics" ? "idle" : "warn", {width_value * 0.50, height_value * 0.23}},
        {"validate_token", "SYMBOL", "ready", {width_value * 0.50, height_value * 0.10}},
        {file_label_, "FILE", "ready", {width_value * 0.22, height_value * 0.36}},
        {"pytest", "TARGET", "idle", {width_value * 0.78, height_value * 0.36}},
        {"pending ChangeSet", "CHANGE", "idle", {width_value * 0.27, height_value * 0.64}},
        {job_label_, "JOB", job_label_ == "No jobs" ? "idle" : "ready", {width_value * 0.73, height_value * 0.64}},
    };
}

}
