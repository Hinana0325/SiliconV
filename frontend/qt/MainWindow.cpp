/*
 * SiliconV — Main Window (Implementation)
 */

#include "MainWindow.h"
#include "VMManager.h"
#include "VMDisplayWidget.h"
#include "ConsoleWidget.h"

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QSlider>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

/* ── MainWindow ────────────────────────────────── */

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("SiliconV");
    resize(1300, 780);
    setMinimumSize(900, 500);

    m_manager = new VMManager(this);

    setupUI();
    setupToolbar();
    setupSidebar();
    setupStatusBar();
    setupMenu();
    setupConnections();
    applyDarkTheme();
}

MainWindow::~MainWindow() {
    m_manager->stop();
}

/* ── UI Layout ─────────────────────────────────── */

void MainWindow::setupUI() {
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);

    /* Sidebar placeholder (set up in setupSidebar) */
    m_deviceTree = new QTreeWidget;
    m_deviceTree->setHeaderHidden(true);
    m_deviceTree->setIndentation(16);
    m_deviceTree->setRootIsDecorated(true);
    m_deviceTree->setAnimated(true);

    /* VM Display */
    m_display = new VMDisplayWidget;
    m_display->setVMManager(m_manager);

    /* Console */
    m_console = new ConsoleWidget;

    m_splitter->addWidget(m_deviceTree);
    m_splitter->addWidget(m_display);
    m_splitter->addWidget(m_console);
    m_splitter->setSizes({180, 560, 400});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 1);

    setCentralWidget(m_splitter);

    /* Drag & drop support */
    setAcceptDrops(true);
}

/* ── Toolbar ───────────────────────────────────── */

void MainWindow::setupToolbar() {
    m_toolbar = addToolBar("Main");
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(22, 22));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_startAction = m_toolbar->addAction(
        QIcon::fromTheme("media-playback-start", QIcon(":/icons/start.png")), "Start");
    m_startAction->setShortcut(QKeySequence("Ctrl+S"));
    connect(m_startAction, &QAction::triggered, this, &MainWindow::startVM);

    m_stopAction = m_toolbar->addAction(
        QIcon::fromTheme("media-playback-stop", QIcon(":/icons/stop.png")), "Stop");
    m_stopAction->setEnabled(false);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopVM);

    m_pauseAction = m_toolbar->addAction(
        QIcon::fromTheme("media-playback-pause", QIcon(":/icons/pause.png")), "Pause");
    m_pauseAction->setEnabled(false);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::pauseVM);

    m_toolbar->addSeparator();

    QAction *kernelAction = m_toolbar->addAction(
        QIcon::fromTheme("document-open"), "Kernel");
    connect(kernelAction, &QAction::triggered, this, &MainWindow::browseKernel);

    QAction *rootfsAction = m_toolbar->addAction(
        QIcon::fromTheme("drive-harddisk"), "Rootfs");
    connect(rootfsAction, &QAction::triggered, this, &MainWindow::browseRootfs);

    m_toolbar->addSeparator();

    QAction *settingsAction = m_toolbar->addAction(
        QIcon::fromTheme("preferences-system"), "Settings");
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
}

/* ── Sidebar (Device Tree) ─────────────────────── */

void MainWindow::setupSidebar() {
    m_deviceTree->setColumnCount(2);
    m_deviceTree->header()->setStretchLastSection(true);

    QString sidebarStyle =
        "QTreeWidget { background-color: #161B22; border: none; color: #C9D1D9; }"
        "QTreeWidget::item { padding: 4px 8px; }"
        "QTreeWidget::item:selected { background-color: #1F6FEB; }"
        "QTreeWidget::branch:has-children:!has-siblings:closed,"
        "QTreeWidget::branch:closed:has-children:has-siblings {"
        "  border-image: none; image: none; }"
        "QTreeWidget::branch:open:has-children:!has-siblings,"
        "QTreeWidget::branch:open:has-children:has-siblings {"
        "  border-image: none; image: none; }";
    m_deviceTree->setStyleSheet(sidebarStyle);

    updateDeviceTree();
}

void MainWindow::updateDeviceTree() {
    m_deviceTree->clear();

    auto addItem = [this](QTreeWidgetItem *parent, const QString &name,
                           const QString &info, bool enabled, bool expandable = false) {
        QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent)
                                       : new QTreeWidgetItem(m_deviceTree);
        item->setText(0, enabled ? "● " + name : "○ " + name);
        item->setText(1, info);
        item->setForeground(0, enabled ? QColor(80, 200, 80) : QColor(100, 100, 100));
        item->setForeground(1, QColor(120, 125, 135));
        item->setFont(1, QFont("Monospace", 8));
        return item;
    };

    addItem(nullptr, "UART (PL011)", "MMIO 0x10000000 IRQ 32", true);
    addItem(nullptr, "GICv3", "0x08000000 8 SPI", true);
    addItem(nullptr, "PSCI", "CPU lifecycle", true);
    addItem(nullptr, "DTB Generator", "Runtime DTB", true);

    auto *vio = addItem(nullptr, "Virtio Devices", "MMIO Transport", true, true);
    addItem(vio, "virtio-blk", m_config.rootfsPath.isEmpty() ? "not attached" : m_config.rootfsPath, !m_config.rootfsPath.isEmpty());
    addItem(vio, "virtio-net", "IRQ 41 MMIO 0x20010000", true);
    addItem(vio, "virtio-gpu", "IRQ 43 MMIO 0x20030000", false);
    addItem(vio, "virtio-console", "IRQ 44 MMIO 0x20040000", true);

    m_deviceTree->expandAll();
}

/* ── Status Bar ────────────────────────────────── */

void MainWindow::setupStatusBar() {
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("color: #8B949E; padding: 2px 8px;");
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->setStyleSheet("QStatusBar { background-color: #161B22; border-top: 1px solid #30363D; }");
}

/* ── Menu Bar ──────────────────────────────────── */

void MainWindow::setupMenu() {
    /* File */
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(QIcon::fromTheme("document-open"), "Open &Kernel…", this, &MainWindow::browseKernel, QKeySequence("Ctrl+O"));
    fileMenu->addAction(QIcon::fromTheme("drive-harddisk"), "Open &Rootfs…", this, &MainWindow::browseRootfs, QKeySequence("Ctrl+R"));
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", qApp, &QApplication::quit, QKeySequence("Ctrl+Q"));

    /* VM */
    QMenu *vmMenu = menuBar()->addMenu("&VM");
    vmMenu->addAction("&Start", this, &MainWindow::startVM, QKeySequence("Ctrl+Return"));
    vmMenu->addAction("S&top", this, &MainWindow::stopVM, QKeySequence("Ctrl+."));
    vmMenu->addAction("&Pause / Resume", this, &MainWindow::pauseVM, QKeySequence("Ctrl+P"));
    vmMenu->addSeparator();
    vmMenu->addAction("Clear &Console", this, &MainWindow::clearConsole, QKeySequence("Ctrl+K"));

    /* View */
    QMenu *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("&Settings…", this, &MainWindow::showSettings, QKeySequence("Ctrl+,"));

    /* Help */
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About SiliconV", this, [this] {
        QMessageBox::about(this, "SiliconV",
            "<h2>SiliconV v0.1</h2>"
            "<p>Virtual Phone Hardware Platform</p>"
            "<p>A hypervisor platform for running AOSP Android<br>"
            "in a virtual machine.</p>"
            "<p>SVABI v0 · GICv3 · PL011 UART · Virtio</p>");
    });
}

/* ── Connections ───────────────────────────────── */

void MainWindow::setupConnections() {
    connect(m_manager, &VMManager::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_manager, &VMManager::consoleByte, this, &MainWindow::onConsoleByte);
    connect(m_manager, &VMManager::consoleOutput, this, &MainWindow::onConsoleOutput);
    connect(m_manager, &VMManager::errorOccurred, this, [this](const QString &msg) {
        QMessageBox::warning(this, "SiliconV", msg);
    });
}

/* ── Slots — VM Control ────────────────────────── */

void MainWindow::startVM() {
    if (m_manager->isRunning()) return;
    if (m_config.kernelPath.isEmpty()) {
        QMessageBox::information(this, "SiliconV", "Please select a kernel image first.");
        return;
    }
    m_manager->start(m_config);
}

void MainWindow::stopVM() { m_manager->stop(); }
void MainWindow::pauseVM() {
    if (m_manager->state() == VMManager::Paused) m_manager->resume();
    else m_manager->pause();
}

/* ── Slots — File Browsing ─────────────────────── */

void MainWindow::browseKernel() {
    QString path = QFileDialog::getOpenFileName(this, "Select Kernel Image",
        QString(), "All Files (*);;Boot Images (*.img);;Binaries (*.bin *.elf)");
    if (!path.isEmpty()) {
        m_config.kernelPath = path;
        m_statusLabel->setText(QString("Kernel: %1").arg(QFileInfo(path).fileName()));
        updateDeviceTree();
    }
}

void MainWindow::browseRootfs() {
    QString path = QFileDialog::getOpenFileName(this, "Select Root Filesystem",
        QString(), "Disk Images (*.img *.qcow2 *.raw *.iso);;All Files (*)");
    if (!path.isEmpty()) {
        m_config.rootfsPath = path;
        updateDeviceTree();
        m_statusLabel->setText(QString("Kernel: %1  |  Rootfs: %2")
            .arg(QFileInfo(m_config.kernelPath).fileName())
            .arg(QFileInfo(path).fileName()));
    }
}

/* ── Settings ──────────────────────────────────── */

void MainWindow::showSettings() {
    SettingsDialog dlg(m_config, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_config = dlg.config();
        m_statusLabel->setText(QString("Config: %1 CPUs, %2 MB RAM")
            .arg(m_config.numCPUs).arg(m_config.ramMB));
    }
}

void MainWindow::clearConsole() { m_console->clearAll(); }

/* ── Slots — State & Console ───────────────────── */

void MainWindow::onStateChanged(VMManager::State state) {
    switch (state) {
        case VMManager::Idle:
            m_statusLabel->setText("Ready");
            m_startAction->setEnabled(true);
            m_stopAction->setEnabled(false);
            m_pauseAction->setEnabled(false);
            m_display->setRunning(false);
            break;
        case VMManager::Starting:
            m_statusLabel->setText("🔄 Starting VM…");
            m_startAction->setEnabled(false);
            break;
        case VMManager::Running:
            m_statusLabel->setText(QString("🟢 Running | CPU: %1 | RAM: %2 MB")
                .arg(m_config.numCPUs).arg(m_config.ramMB));
            m_startAction->setEnabled(false);
            m_stopAction->setEnabled(true);
            m_pauseAction->setEnabled(true);
            m_display->setRunning(true);
            break;
        case VMManager::Paused:
            m_statusLabel->setText("⏸ Paused");
            m_startAction->setEnabled(true);
            m_stopAction->setEnabled(true);
            m_pauseAction->setText("Resume");
            break;
        case VMManager::Stopping:
            m_statusLabel->setText("Stopping VM…");
            break;
        case VMManager::Error:
            m_statusLabel->setText("✗ VM Error — check console");
            m_startAction->setEnabled(true);
            m_stopAction->setEnabled(false);
            m_display->setRunning(false);
            break;
    }
}

void MainWindow::onConsoleByte(uint8_t byte) {
    m_console->kernelTab()->appendByte(byte);
}

void MainWindow::onConsoleOutput(const QString &line) {
    m_console->kernelTab()->appendText(line);
}

/* ── Dark Theme (QSS) ──────────────────────────── */

void MainWindow::applyDarkTheme() {
    setStyleSheet(R"(
        QMainWindow {
            background-color: #0D1117;
        }
        QToolBar {
            background-color: #161B22;
            border-bottom: 1px solid #30363D;
            spacing: 4px;
            padding: 4px;
        }
        QToolBar QToolButton {
            color: #C9D1D9;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 4px 10px;
            margin: 1px;
        }
        QToolBar QToolButton:hover {
            background-color: #1F6FEB;
            border-color: #1F6FEB;
            color: #FFFFFF;
        }
        QToolBar QToolButton:pressed {
            background-color: #1A5FCC;
        }
        QToolBar QToolButton:disabled {
            color: #484F58;
        }
        QMenuBar {
            background-color: #161B22;
            color: #C9D1D9;
            border-bottom: 1px solid #21262D;
        }
        QMenuBar::item:selected {
            background-color: #1F6FEB;
        }
        QMenu {
            background-color: #161B22;
            color: #C9D1D9;
            border: 1px solid #30363D;
        }
        QMenu::item:selected {
            background-color: #1F6FEB;
        }
        QSplitter::handle {
            background-color: #30363D;
        }
        QTreeWidget {
            background-color: #161B22;
            color: #C9D1D9;
            border: none;
        }
        QTreeWidget::item:hover {
            background-color: #1F2A3A;
        }
        QStatusBar {
            background-color: #161B22;
            color: #8B949E;
            border-top: 1px solid #30363D;
        }
        QDialog {
            background-color: #161B22;
            color: #C9D1D9;
        }
        QLabel {
            color: #C9D1D9;
        }
        QLineEdit, QSpinBox, QSlider {
            background-color: #0D1117;
            color: #C9D1D9;
            border: 1px solid #30363D;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QPushButton {
            background-color: #1F6FEB;
            color: #FFFFFF;
            border: none;
            border-radius: 6px;
            padding: 6px 20px;
        }
        QPushButton:hover {
            background-color: #388BFD;
        }
        QPushButton:pressed {
            background-color: #1A5FCC;
        }
        QTabWidget::pane {
            border: none;
            background-color: #0D1117;
        }
        QTabBar::tab {
            background-color: #161B22;
            color: #8B949E;
            border: none;
            padding: 6px 16px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background-color: #0D1117;
            color: #58A6FF;
            border-bottom: 2px solid #58A6FF;
        }
        QTabBar::tab:hover {
            color: #C9D1D9;
        }
        QGroupBox {
            color: #C9D1D9;
            border: 1px solid #30363D;
            border-radius: 8px;
            margin-top: 12px;
            padding-top: 16px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 4px;
        }
    )");
}

/* ── Drag & Drop ───────────────────────────────── */

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        QString path = url.toLocalFile();
        QString ext = QFileInfo(path).suffix().toLower();

        if (ext == "img" || ext == "qcow2" || ext == "raw" || ext == "iso") {
            m_config.rootfsPath = path;
        } else {
            m_config.kernelPath = path;
        }
    }

    updateDeviceTree();
    m_statusLabel->setText(QString("Kernel: %1  |  Rootfs: %2")
        .arg(QFileInfo(m_config.kernelPath).fileName())
        .arg(QFileInfo(m_config.rootfsPath).fileName()));
}

QString MainWindow::formatSize(qint64 bytes) const {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024);
    return QString("%1 MB").arg(bytes / (1024 * 1024));
}

/* ── Settings Dialog ────────────────────────────── */

SettingsDialog::SettingsDialog(VMManager::Config &config, QWidget *parent)
    : QDialog(parent), m_config(config) {
    setWindowTitle("VM Configuration");
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    /* CPU */
    auto *cpuGroup = new QGroupBox("Hardware", this);
    auto *cpuLayout = new QFormLayout(cpuGroup);

    auto *cpuSpin = new QSpinBox(this);
    cpuSpin->setRange(1, 8);
    cpuSpin->setValue(config.numCPUs);
    cpuSpin->setPrefix("vCPU ");
    cpuLayout->addRow("Processors:", cpuSpin);

    auto *ramSlider = new QSlider(Qt::Horizontal, this);
    ramSlider->setRange(512, 16384);
    ramSlider->setValue(config.ramMB);
    ramSlider->setTickPosition(QSlider::TicksBelow);
    ramSlider->setTickInterval(4096);

    auto *ramLabel = new QLabel(QString("%1 MB").arg(config.ramMB), this);
    QObject::connect(ramSlider, &QSlider::valueChanged, ramLabel,
        [ramLabel](int v) { ramLabel->setText(QString("%1 MB").arg(v)); });

    auto *ramRow = new QHBoxLayout;
    ramRow->addWidget(ramSlider);
    ramRow->addWidget(ramLabel);
    cpuLayout->addRow("RAM:", ramRow);

    layout->addWidget(cpuGroup);

    /* Boot */
    auto *bootGroup = new QGroupBox("Boot", this);
    auto *bootLayout = new QFormLayout(bootGroup);

    auto *cmdEdit = new QLineEdit(config.cmdline, this);
    cmdEdit->setFont(QFont("Monospace", 9));
    bootLayout->addRow("Kernel Cmdline:", cmdEdit);

    auto *dryCheck = new QCheckBox("Dry Run (validate without running vCPU)", this);
    dryCheck->setChecked(config.dryRun);
    bootLayout->addRow(dryCheck);

    layout->addWidget(bootGroup);

    /* Buttons */
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, this, [&, cpuSpin, ramSlider, cmdEdit, dryCheck] {
        m_config.numCPUs = cpuSpin->value();
        m_config.ramMB   = ramSlider->value();
        m_config.cmdline = cmdEdit->text();
        m_config.dryRun  = dryCheck->isChecked();
        accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
