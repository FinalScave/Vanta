#include <QApplication>
#include <QPixmap>
#include <QRegularExpression>
#include <QString>
#include <QTimer>

#include "main_window.h"
#include "theme.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    mornox::ide::ApplyDarkTheme(app);

    mornox::ide::MainWindow window;
    QString screenshot_path;
    QSize screenshot_size(1680, 945);
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == "--screenshot" && i + 1 < argc) {
            screenshot_path = QString::fromLocal8Bit(argv[++i]);
        } else if (argument == "--screenshot-size" && i + 1 < argc) {
            const QString value = QString::fromLocal8Bit(argv[++i]);
            const QRegularExpressionMatch match = QRegularExpression(R"((\d+)x(\d+))").match(value);
            if (match.hasMatch()) {
                screenshot_size = QSize(match.captured(1).toInt(), match.captured(2).toInt());
            }
        }
    }

    if (!screenshot_path.isEmpty()) {
        window.resize(screenshot_size);
        window.show();
        QTimer::singleShot(300, &app, [&app, &window, screenshot_path] {
            const QPixmap pixmap = window.grab();
            pixmap.save(screenshot_path);
            app.quit();
        });
        return app.exec();
    }

    window.show();
    return app.exec();
}
