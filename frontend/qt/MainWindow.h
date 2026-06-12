/*
 * SiliconV — Linux Qt6 Main Window
 *
 * Central hub: QSplitter three-column layout, toolbar, sidebar, status bar.
 * Coordinates VMDisplayWidget, ConsoleWidget, and VMManager.
 */

#ifndef SILICONV_MAINWINDOW_H
#define SILICONV_MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QTreeWidget>
#include <QToolBar>
#include <QLabel>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QSlider>

#include "VMManager.h"
class VMDisplayWidget;
class ConsoleWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void startVM();
    void stopVM();
    void pauseVM();
    void browseKernel();
    void browseRootfs();
    void showSettings();
    void clearConsole();
    void onStateChanged(VMManager::State state);
    void onConsoleByte(uint8_t byte);
    void onConsoleOutput(const QString &line);

private:
    void setupUI();
    void setupToolbar();
    void setupSidebar();
    void setupStatusBar();
    void setupMenu();
    void setupConnections();
    void applyDarkTheme();
    void updateDeviceTree();
    QString formatSize(qint64 bytes) const;

    /* Widgets */
    QSplitter        *m_splitter;
    QTreeWidget      *m_deviceTree;
    VMDisplayWidget  *m_display;
    ConsoleWidget    *m_console;
    QLabel           *m_statusLabel;
    QToolBar         *m_toolbar;
    QAction          *m_startAction;
    QAction          *m_stopAction;
    QAction          *m_pauseAction;

    /* Engine */
    VMManager        *m_manager;
    VMManager::Config m_config;
};

/* ── Settings Dialog ────────────────────────────── */

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(VMManager::Config &config, QWidget *parent = nullptr);
    VMManager::Config config() const { return m_config; }

private:
    VMManager::Config &m_config;
};

#endif // SILICONV_MAINWINDOW_H
