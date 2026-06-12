/*
 * SiliconV — Console Widget (Implementation)
 */

#include "ConsoleWidget.h"
#include <QVBoxLayout>
#include <QScrollBar>
#include <QFont>
#include <QDateTime>
#include <QTextCursor>
#include <QTimer>

/* ── ConsoleTab ────────────────────────────────── */

ConsoleTab::ConsoleTab(QWidget *parent)
    : QPlainTextEdit(parent) {
    setReadOnly(true);
    setFont(QFont("Monospace", 11));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    /* Flush buffer every 50ms */
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (!m_lineBuffer.isEmpty()) flushBuffer();
    });
    timer->start(50);
}

void ConsoleTab::appendByte(uint8_t byte) {
    if (byte == '\n') {
        m_lineBuffer += '\n';
        flushBuffer();
    } else if (byte >= 0x20 && byte < 0x7F) {
        m_lineBuffer += QChar(byte);
    } else if (byte == '\t') {
        m_lineBuffer += "    ";
    } else if (byte == '\r') {
        /* skip */
    } else {
        m_lineBuffer += QString("[0x%1]").arg(byte, 2, 16, QChar('0')).toUpper();
    }
}

void ConsoleTab::appendText(const QString &text) {
    m_lineBuffer += text;
    if (text.contains('\n')) flushBuffer();
}

void ConsoleTab::flushBuffer() {
    if (m_lineBuffer.isEmpty()) return;

    bool atBottom = verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 10;

    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(m_lineBuffer);
    m_lineBuffer.clear();

    if (atBottom) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }

    trimIfNeeded();
}

void ConsoleTab::trimIfNeeded() {
    int lines = document()->lineCount();
    if (lines > m_maxLines) {
        QTextCursor cursor(document());
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor,
                             lines - m_maxLines);
        cursor.removeSelectedText();
    }
}

/* ── ConsoleWidget ─────────────────────────────── */

ConsoleWidget::ConsoleWidget(QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(true);

    m_kernelTab  = new ConsoleTab(this);
    m_androidTab = new ConsoleTab(this);
    m_logcatTab  = new ConsoleTab(this);

    /* Styling */
    QString tabStyle =
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { padding: 4px 16px; }";

    m_kernelTab->setStyleSheet(
        "QPlainTextEdit { background-color: #0D1117; color: #00FF00; "
        "border: none; selection-background-color: #1F6FEB; }");

    m_androidTab->setStyleSheet(
        "QPlainTextEdit { background-color: #14161A; color: #D9D9D9; "
        "border: none; selection-background-color: #1F6FEB; }");

    m_logcatTab->setStyleSheet(
        "QPlainTextEdit { background-color: #1A1C20; color: #D9D9D9; "
        "border: none; selection-background-color: #1F6FEB; }");

    m_tabWidget->setStyleSheet(tabStyle);

    m_tabWidget->addTab(m_kernelTab,  QIcon::fromTheme("utilities-terminal"), "Kernel");
    m_tabWidget->addTab(m_androidTab, QIcon::fromTheme("applications-system"), "Android");
    m_tabWidget->addTab(m_logcatTab,  QIcon::fromTheme("text-x-log"), "Logcat");

    layout->addWidget(m_tabWidget);
}

void ConsoleWidget::clearAll() {
    m_kernelTab->clear();
    m_androidTab->clear();
    m_logcatTab->clear();
}
