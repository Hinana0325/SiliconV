/*
 * SiliconV — Linux Qt6 VM Display Widget
 *
 * Custom-painted VM display area with:
 *   - SiliconV brand splash when idle
 *   - Pulsing indicator when VM is running
 *   - Keyboard capture for guest UART input
 */

#ifndef SILICONV_VMDISPLAYWIDGET_H
#define SILICONV_VMDISPLAYWIDGET_H

#include <QWidget>
#include <QTimer>

class VMManager;

class VMDisplayWidget : public QWidget {
    Q_OBJECT
public:
    explicit VMDisplayWidget(QWidget *parent = nullptr);

    void setVMManager(VMManager *mgr) { m_manager = mgr; }
    void setRunning(bool running);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void drawBranding(QPainter &p, const QRect &r);
    void drawPulseRing(QPainter &p, const QPointF &center);

    VMManager *m_manager = nullptr;
    QTimer    *m_animTimer;
    double     m_animPhase = 0.0;
    bool       m_running = false;
};

#endif // SILICONV_VMDISPLAYWIDGET_H
