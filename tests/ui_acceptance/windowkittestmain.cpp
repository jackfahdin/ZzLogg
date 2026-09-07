#include <QApplication>
#include <QtTest>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>
void verifyTitleFormatting();
void verifyActiveDocumentTitleSynchronization();
void verifyChromeStateAndIconSynchronization();
void verifyWindowButtonIntents();
void verifyAlwaysOnTopPreservesWindowPresentation();
class WindowChromeBehaviorTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void TitleFormatting() { verifyTitleFormatting(); }
    void ActiveDocumentTitleSynchronization() { verifyActiveDocumentTitleSynchronization(); }
    void ChromeStateAndIconSynchronization() { verifyChromeStateAndIconSynchronization(); }
    void WindowButtonIntents() { verifyWindowButtonIntents(); }
    void AlwaysOnTopPreservesWindowPresentation() { verifyAlwaysOnTopPreservesWindowPresentation(); }
};
int main(int argc, char* argv[]) {
    if (!ZzWindowKit::ZzWindowKitBootstrap::prepare()) return 2;
    QApplication app(argc, argv);
    WindowChromeBehaviorTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "windowkittestmain.moc"
