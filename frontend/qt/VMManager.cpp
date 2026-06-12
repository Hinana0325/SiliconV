/*
 * SiliconV — VMManager (Implementation)
 */

#include "VMManager.h"
#include <QDebug>
#include <cstring>

extern "C" {
#include "../../devices/uart/pl011.h"
}

VMManager::VMManager(QObject *parent)
    : QObject(parent) {}

VMManager::~VMManager() {
    stop();
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
    }
}

void VMManager::setState(State state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(state);
}

/* ── Start ─────────────────────────────────────── */

bool VMManager::start(const Config &config) {
    if (m_state != Idle) {
        emit errorOccurred("VM is already running");
        return false;
    }

    m_config = config;
    m_stopRequested = false;
    setState(Starting);

    m_thread = new QThread(this);
    connect(m_thread, &QThread::started, this, &VMManager::runVM);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();

    return true;
}

/* ── VM Execution (on worker thread) ──────────── */

void VMManager::runVM() {
    uint64_t ramSize = (uint64_t)m_config.ramMB * 1024ULL * 1024ULL;

    emitOutput("\n══ SiliconV v0.1 — Starting VM ══\n\n");

    /* Init machine */
    if (sv_machine_init(&m_machine, m_config.numCPUs, ramSize) < 0) {
        emitOutput("✗ Failed to initialize VM\n");
        QMetaObject::invokeMethod(this, [this] { setState(Error); }, Qt::QueuedConnection);
        return;
    }
    m_initialized = true;

    /* UART callback */
    pl011_set_tx_callback(&m_machine.uart, uartTxCallback, this);

    emitOutput(QString("✓ Machine initialized (%1 CPUs, %2 MB RAM)\n")
                   .arg(m_config.numCPUs).arg(m_config.ramMB));

    /* Load kernel */
    if (!m_config.kernelPath.isEmpty()) {
        if (sv_machine_load_kernel(&m_machine, m_config.kernelPath.toUtf8().constData()) < 0) {
            emitOutput("✗ Failed to load kernel\n");
            QMetaObject::invokeMethod(this, [this] { setState(Error); }, Qt::QueuedConnection);
            goto cleanup;
        }
        emitOutput(QString("✓ Kernel loaded: %1\n").arg(m_config.kernelPath));
    }

    /* DTB */
    if (!m_config.dtbPath.isEmpty()) {
        sv_machine_load_dtb(&m_machine, m_config.dtbPath.toUtf8().constData());
    } else {
        if (!m_config.cmdline.isEmpty()) {
            m_machine.dtb_config.cmdline = strdup(m_config.cmdline.toUtf8().constData());
        }
        if (sv_machine_generate_dtb(&m_machine) < 0) {
            emitOutput("✗ Failed to generate DTB\n");
            QMetaObject::invokeMethod(this, [this] { setState(Error); }, Qt::QueuedConnection);
            goto cleanup;
        }
        emitOutput("✓ DTB generated\n");
    }

    /* Attach rootfs */
    if (!m_config.rootfsPath.isEmpty()) {
        if (sv_machine_attach_virtio_blk(&m_machine, m_config.rootfsPath.toUtf8().constData(), false) < 0) {
            emitOutput("⚠ Failed to attach virtio-blk (continuing)\n");
        } else {
            emitOutput(QString("✓ virtio-blk: %1\n").arg(m_config.rootfsPath));
        }
    }

    /* Attach net + console */
    sv_machine_attach_virtio_net(&m_machine);
    sv_machine_attach_virtio_console(&m_machine);

    emitOutput("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    if (m_config.dryRun) {
        emitOutput("Dry run complete — configuration is valid\n");
        QMetaObject::invokeMethod(this, [this] { setState(Idle); }, Qt::QueuedConnection);
        goto cleanup;
    }

    QMetaObject::invokeMethod(this, [this] { setState(Running); }, Qt::QueuedConnection);
    emitOutput("Entering VM main loop...\n\n");

    {
        int ret = sv_machine_run(&m_machine);
        if (ret < 0) {
            emitOutput("✗ VM exited with error\n");
            QMetaObject::invokeMethod(this, [this] { setState(Error); }, Qt::QueuedConnection);
        } else {
            emitOutput("\n══ VM stopped ══\n");
            QMetaObject::invokeMethod(this, [this] { setState(Idle); }, Qt::QueuedConnection);
        }
    }

cleanup:
    if (m_initialized) {
        sv_machine_destroy(&m_machine);
        m_initialized = false;
    }
}

/* ── Stop / Pause / Resume ────────────────────── */

void VMManager::stop() {
    if (m_state == Idle || m_state == Error) return;
    setState(Stopping);
    m_stopRequested = true;
    if (m_initialized) sv_machine_stop(&m_machine);
}

void VMManager::pause() {
    if (m_state != Running) return;
    if (m_initialized) m_machine.running = false;
    setState(Paused);
}

void VMManager::resume() {
    if (m_state != Paused) return;
    m_machine.running = true;
    setState(Running);
}

/* ── Keyboard ──────────────────────────────────── */

void VMManager::sendKey(int key) {
    if (!m_initialized || !m_machine.running) return;

    if (key == '\r' || key == '\n') {
        pl011_rx_put(&m_machine.uart, '\r');
    } else if (key == Qt::Key_Backspace || key == 0x7F) {
        pl011_rx_put(&m_machine.uart, 0x08);
    } else if (key < 0x80) {
        pl011_rx_put(&m_machine.uart, (uint8_t)key);
    }
}

/* ── Output helpers ────────────────────────────── */

void VMManager::emitOutput(const QString &line) {
    QMetaObject::invokeMethod(this, [this, line] {
        emit consoleOutput(line);
    }, Qt::QueuedConnection);
}

void VMManager::uartTxCallback(uint8_t byte, void *ctx) {
    auto *self = static_cast<VMManager *>(ctx);
    QMetaObject::invokeMethod(self, [self, byte] {
        emit self->consoleByte(byte);
    }, Qt::QueuedConnection);
}
