#include "icons.h"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

namespace mornox::ide {

QPixmap IconPixmap(const QString& name, const QColor& color, const QSize& size) {
    QFile file(QString(":/icons/%1.svg").arg(name));
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    if (!file.open(QIODevice::ReadOnly)) {
        return pixmap;
    }

    QByteArray svg = file.readAll();
    svg.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svg);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(size)));
    return pixmap;
}

QIcon Icon(const QString& name, const QColor& color, const QSize& size) {
    return QIcon(IconPixmap(name, color, size));
}

}
