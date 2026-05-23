#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace mornox::ide {

QPixmap IconPixmap(const QString& name, const QColor& color = QColor("#bff7f2"), const QSize& size = QSize(18, 18));
QIcon Icon(const QString& name, const QColor& color = QColor("#bff7f2"), const QSize& size = QSize(18, 18));

}
