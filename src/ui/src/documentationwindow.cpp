#include "documentationwindow.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QScreen>
#include <QScrollBar>
#include <QTextDocument>

DocumentationWindow::DocumentationWindow(QWidget* parent) : QTextBrowser(parent)
{
    setObjectName(QStringLiteral("documentationWindow"));
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose);
    setOpenExternalLinks(true);
    auto readingFont = font();
    readingFont.setPointSize(11);
    setFont(readingFont);
    document()->setDocumentMargin(24);
    const QSize available = parent->screen()->availableGeometry().size() - QSize(48, 80);
    resize(QSize(840, 720).boundedTo(available));
    reload();
}

void DocumentationWindow::changeEvent(QEvent* event)
{
    QTextBrowser::changeEvent(event);
    if (event->type() == QEvent::LanguageChange || event->type() == QEvent::PaletteChange
        || event->type() == QEvent::FontChange) {
        reload();
    }
}

void DocumentationWindow::reload()
{
    // This locale marker follows the installed translator, not an unsaved preference.
    QString locale = QCoreApplication::translate("DocumentationWindow", "en");
    if (locale != "zh_CN" && locale != "zh_TW") locale = "en";
    QFile file(QStringLiteral(":/documentation/%1.html").arg(locale));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const int maximum = verticalScrollBar()->maximum();
    const double position = maximum > 0 ? double(verticalScrollBar()->value()) / maximum : 0;
    const QColor background = palette().color(QPalette::Base);
    const bool dark = background.lightness() < 128;
    document()->setDefaultStyleSheet(QStringLiteral(
        "body { color: %1; } "
        "h1 { font-size: 24pt; margin-bottom: 20px; } "
        "h2 { font-size: 18pt; margin-top: 28px; margin-bottom: 12px; } "
        "h3 { font-size: 14pt; margin-top: 20px; } "
        "p, li { margin-top: 8px; margin-bottom: 8px; } "
        "a { color: %2; } "
        "code, pre { font-family: Consolas, monospace; color: %1; background-color: %3; } "
        "th { background-color: %3; } "
        "td, th { padding: 8px; }")
        .arg(palette().color(QPalette::Text).name(),
             dark ? QStringLiteral("#75bfff") : QStringLiteral("#0064b4"),
             (dark ? background.lighter(130) : background.darker(105)).name()));
    setHtml(QString::fromUtf8(file.readAll()));
    setWindowTitle(QCoreApplication::translate("MainWindow", "%1 documentation")
                       .arg(QStringLiteral("ZzLogg")));
    verticalScrollBar()->setValue(qRound(position * verticalScrollBar()->maximum()));
}
