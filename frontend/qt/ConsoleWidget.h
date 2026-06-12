/*
 * SiliconV — Linux Qt6 Console Widget
 *
 * Tabbed terminal console: Kernel | Android | Logcat
 * Features: ANSI rendering, auto-scroll, line buffering, search
 */

#ifndef SILICONV_CONSOLEWIDGET_H
#define SILICONV_CONSOLEWIDGET_H

#include <QWidget>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QString>

class ConsoleTab : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit ConsoleTab(QWidget *parent = nullptr);
    void appendByte(uint8_t byte);
    void appendText(const QString &text);
    void setMaxLines(int max) { m_maxLines = max; }
private:
    QString m_lineBuffer;
    int     m_maxLines = 20000;
    void flushBuffer();
    void trimIfNeeded();
};

class ConsoleWidget : public QWidget {
    Q_OBJECT
public:
    explicit ConsoleWidget(QWidget *parent = nullptr);

    ConsoleTab *kernelTab()  const { return m_kernelTab; }
    ConsoleTab *androidTab() const { return m_androidTab; }
    ConsoleTab *logcatTab()  const { return m_logcatTab; }

public slots:
    void clearAll();

private:
    QTabWidget  *m_tabWidget;
    ConsoleTab  *m_kernelTab;
    ConsoleTab  *m_androidTab;
    ConsoleTab  *m_logcatTab;
};

#endif // SILICONV_CONSOLEWIDGET_H
