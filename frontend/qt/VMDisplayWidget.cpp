/*
 * SiliconV — VM Display Widget (Implementation)
 */

#include "VMDisplayWidget.h"
#include "VMManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QtMath>

VMDisplayWidget::VMDisplayWidget(QWidget *parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    /* Animation timer — 60fps for smooth pulse */
    m_animTimer = new QTimer(this);
    connect(m_animTimer, &QTimer::timeout, this, [this] {
        m_animPhase += 0.05;
        update();
    });
    m_animTimer->start(16);
}

void VMDisplayWidget::setRunning(bool running) {
    if (m_running == running) return;
    m_running = running;
    update();
}

/* ── Paint ─────────────────────────────────────── */

void VMDisplayWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    /* Dark background */
    p.fillRect(rect(), QColor(13, 17, 23));

    if (m_running) {
        /* Pulsing ring when VM is running */
        drawPulseRing(p, QPointF(width() / 2.0, height() / 2.0));
        /* Dimmed branding */
        p.setOpacity(0.3);
        drawBranding(p, rect());
        p.setOpacity(1.0);

        /* Running indicator text */
        p.setPen(QColor(50, 230, 80));
        p.setFont(QFont("Monospace", 12));
        p.drawText(rect().adjusted(0, height() - 36, 0, 0),
                    Qt::AlignHCenter | Qt::AlignBottom,
                    "⚡ VM Running");
    } else {
        drawBranding(p, rect());
    }
}

void VMDisplayWidget::drawBranding(QPainter &p, const QRect &r) {
    int cx = r.center().x();
    int cy = r.center().y();

    /* Title: SILICON V */
    QFont titleFont("Monospace", 36, QFont::Bold);
    p.setFont(titleFont);

    /* Gold gradient text */
    QLinearGradient goldGrad(0, cy - 30, 0, cy);
    goldGrad.setColorAt(0, QColor(255, 215, 0));
    goldGrad.setColorAt(1, QColor(200, 150, 30));
    QPen goldPen(QBrush(goldGrad), 2);
    p.setPen(goldPen);

    QRect titleRect(0, cy - 30, r.width(), 48);
    p.drawText(titleRect, Qt::AlignHCenter, "SILICON  V");

    /* Subtitle */
    p.setPen(QColor(120, 130, 150));
    p.setFont(QFont("Monospace", 11));
    QRect subRect(0, cy + 8, r.width(), 24);
    p.drawText(subRect, Qt::AlignHCenter, "Virtual Phone Hardware Platform");

    /* CPU Core indicators */
    QFont coreFont("Monospace", 9);
    p.setFont(coreFont);
    QStringList cores = {"⚫ Core A", "⚫ Core B", "⚫ Core C", "⚫ Core D"};
    QList<QColor> coreColors = {
        QColor(80, 180, 255),
        QColor(255, 130, 80),
        QColor(80, 255, 140),
        QColor(220, 100, 200)
    };

    int coreY = cy + 48;
    int totalW = cores.size() * 90;
    int startX = cx - totalW / 2;

    for (int i = 0; i < cores.size(); i++) {
        p.setPen(coreColors[i]);
        QRect coreRect(startX + i * 90, coreY, 90, 20);
        p.drawText(coreRect, Qt::AlignCenter, cores[i]);
    }

    /* Bottom tagline */
    p.setPen(QColor(80, 85, 95));
    p.setFont(QFont("Monospace", 9));
    QRect tagRect(0, coreY + 28, r.width(), 16);
    p.drawText(tagRect, Qt::AlignHCenter, "SVABI v0 · GICv3 · PL011 UART · Virtio · Android AOSP");
}

void VMDisplayWidget::drawPulseRing(QPainter &p, const QPointF &center) {
    double scale = 1.0 + 0.4 * qSin(m_animPhase);
    double opacity = 0.15 + 0.08 * qSin(m_animPhase * 1.5);

    p.save();
    p.translate(center);
    p.scale(scale, scale);

    QColor ringColor(255, 215, 0);
    ringColor.setAlphaF(opacity);
    p.setPen(Qt::NoPen);
    p.setBrush(ringColor);
    p.drawEllipse(QPointF(0, 0), 60, 60);

    /* Inner ring */
    ringColor.setAlphaF(opacity * 1.8);
    p.setBrush(ringColor);
    p.drawEllipse(QPointF(0, 0), 30, 30);

    p.restore();
}

/* ── Keyboard ──────────────────────────────────── */

void VMDisplayWidget::keyPressEvent(QKeyEvent *event) {
    if (!m_running || !m_manager) {
        QWidget::keyPressEvent(event);
        return;
    }

    int key = event->key();
    QString text = event->text();

    if (!text.isEmpty()) {
        for (const QChar &ch : text) {
            m_manager->sendKey(ch.unicode());
        }
    } else {
        /* Special keys */
        switch (key) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                m_manager->sendKey('\r'); break;
            case Qt::Key_Backspace:
                m_manager->sendKey(0x08); break;
            case Qt::Key_Tab:
                m_manager->sendKey('\t'); break;
            case Qt::Key_Escape:
                m_manager->sendKey(0x1B); break;
            default: break;
        }
    }
}

void VMDisplayWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    update();
}
