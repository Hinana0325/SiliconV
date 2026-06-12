/*
 * SiliconV — Cross-Platform Qt6 Entry Point
 *
 * Works on Windows, Linux, and macOS.
 * Uses Fusion style for consistent dark theme across all platforms.
 */

#include <QApplication>
#include <QStyleFactory>
#include <QDir>
#include "MainWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
/* Windows: prefer UTF-8 for file paths */
#pragma execution_character_set("utf-8")
#endif

int main(int argc, char *argv[]) {
    /* High-DPI support */
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName("SiliconV");
    app.setApplicationVersion("0.1");
    app.setOrganizationName("SiliconV");

#ifdef Q_OS_WIN
    /* Windows: use app-local Qt plugin path if bundled */
    QString pluginPath = QApplication::applicationDirPath() + "/plugins";
    if (QDir(pluginPath).exists()) {
        QApplication::addLibraryPath(pluginPath);
    }
#endif

    /* Fusion style = consistent dark theme on all platforms */
    if (QStyleFactory::keys().contains("Fusion")) {
        app.setStyle("Fusion");
    }

    MainWindow window;
    window.show();

#ifdef Q_OS_WIN
    /* Bring window to foreground on Windows */
    HWND hwnd = reinterpret_cast<HWND>(window.winId());
    SetForegroundWindow(hwnd);
#endif

    return app.exec();
}
