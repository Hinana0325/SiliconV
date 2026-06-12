/*
 * SiliconV — Linux Qt6 VM Manager
 *
 * Thread-safe wrapper around the C machine API.
 * Runs sv_machine_run() on a QThread and emits signals
 * for UI updates. Bridges UART output to ConsoleWidget.
 */

#ifndef SILICONV_VMMANAGER_H
#define SILICONV_VMMANAGER_H

#include <QObject>
#include <QThread>
#include <QString>

extern "C" {
#include "../../core/vm/machine.h"
}

class VMManager : public QObject {
    Q_OBJECT

public:
    enum State { Idle, Starting, Running, Paused, Stopping, Error };
    Q_ENUM(State)

    struct Config {
        QString kernelPath;
        QString rootfsPath;
        QString dtbPath;
        QString cmdline = "console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw";
        int     numCPUs = 4;
        int     ramMB   = 4096;
        bool    dryRun  = false;
    };

    explicit VMManager(QObject *parent = nullptr);
    ~VMManager() override;

    State state() const { return m_state; }
    bool isRunning() const { return m_state == Running; }
    const Config &config() const { return m_config; }

public slots:
    bool start(const Config &config);
    void stop();
    void pause();
    void resume();
    void sendKey(int key);

signals:
    void stateChanged(VMManager::State state);
    void consoleByte(uint8_t byte);
    void consoleOutput(const QString &line);
    void errorOccurred(const QString &message);

private:
    void runVM();
    void setState(State state);
    void emitOutput(const QString &line);

    static void uartTxCallback(uint8_t byte, void *ctx);

    Config              m_config;
    State               m_state = Idle;
    QThread            *m_thread = nullptr;
    sv_machine_t        m_machine;
    bool                m_initialized = false;
    bool                m_stopRequested = false;
};

#endif // SILICONV_VMMANAGER_H
