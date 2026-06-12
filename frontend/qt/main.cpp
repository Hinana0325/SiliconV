/*
 * SiliconV — Linux Qt6 Entry Point
 *
 * Initializes the Qt application, applies dark theme,
 * and launches the MainWindow.
 */

#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("SiliconV");
    app.setApplicationVersion("0.1");
    app.setOrganizationName("SiliconV");

    /* Use Fusion style for consistent cross-platform dark theme */
    app.setStyle("Fusion");

    MainWindow window;
    window.show();

    return app.exec();
}
