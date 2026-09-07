#include <QtTest>
#include <QCloseEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QToolButton>
#include <ZzCore/ZzError.h>
#include <ZzCore/ZzErrorCode.h>
#include <ZzFluentUI/ZzFluentTitleBar.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzWindowKit/ZzWindowAgent.h>
#include "windowchrome.h"

class CloseProbe : public QMainWindow {
public:
    int closes = 0;
    void closeEvent(QCloseEvent* event) override { ++closes; event->ignore(); }
};

class WindowChromeTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void formatsEmptyAndLongTitles() {
        QCOMPARE(formatWindowTitle({}), QStringLiteral("ZzLogg"));
        const QString name(512, QLatin1Char('x'));
        QCOMPARE(formatWindowTitle(name), name + QStringLiteral(" — ZzLogg"));
    }
    void catchesUnknownConfiguratorExceptions() {
        ZzFluentUI::ZzThemeController theme;
        QMainWindow window;
        const auto flags = window.windowFlags();
        WindowChrome chrome(window, {theme},
            [](QMainWindow&, ZzFluentUI::ZzFluentTitleBar&,
               ZzWindowKit::ZzWindowAgent&) -> ZzCore::ZzResult<void> { throw 1729; });
        QVERIFY(chrome.usesNativeFallback());
        QCOMPARE(window.windowFlags(), flags);
        QVERIFY(!window.findChild<ZzFluentUI::ZzFluentTitleBar*>());
    }
    void createsMenuBeforeBusinessActions() {
        ZzFluentUI::ZzThemeController theme;
        QMainWindow window;
        WindowChrome chrome(window, {theme});
        QVERIFY(!chrome.usesNativeFallback());
        auto* title = qobject_cast<ZzFluentUI::ZzFluentTitleBar*>(window.menuWidget());
        QVERIFY(title);
        QCOMPARE(&chrome.commandMenuBar(), title->menuBar());
        QVERIFY(chrome.commandMenuBar().actions().isEmpty());
        auto* menu = chrome.commandMenuBar().addMenu("Encoding");
        menu->addAction("Auto")->setCheckable(true);
        QCOMPARE(menu->windowType(), Qt::Popup);
        QVERIFY(title->menuBar()->actions().contains(menu->menuAction()));
        chrome.setDocumentName(QString::fromUtf8("服务器-🚀.log"));
        QCOMPARE(window.windowTitle(), QString::fromUtf8("服务器-🚀.log — ZzLogg"));
    }
    void fallsBackAfterAttachOrConfigureFailure_data() {
        QTest::addColumn<bool>("attachFirst");
        QTest::newRow("attach-failure") << false;
        QTest::newRow("configure-failure") << true;
    }
    void fallsBackAfterAttachOrConfigureFailure() {
        QFETCH(bool, attachFirst);
        ZzFluentUI::ZzThemeController theme;
        QMainWindow window;
        const auto flags = window.windowFlags();
        WindowChrome chrome(window, {theme},
            [attachFirst](QMainWindow& host, ZzFluentUI::ZzFluentTitleBar&,
                          ZzWindowKit::ZzWindowAgent& agent) {
                if (attachFirst) {
                    auto result = agent.attach(&host);
                    if (!result) return result;
                }
                return ZzCore::ZzResult<void>::failure(ZzCore::ZzError(
                    ZzCore::ZzErrorCode::InvalidState, QStringLiteral("forced failure")));
            });
        QVERIFY(chrome.usesNativeFallback());
        QCOMPARE(window.windowFlags(), flags);
        QCOMPARE(window.menuWidget(), &chrome.commandMenuBar());
        QVERIFY(!window.findChild<ZzFluentUI::ZzFluentTitleBar*>());
        auto* menu = chrome.commandMenuBar().addMenu("File");
        QVERIFY(menu->addAction("Open"));
        window.show();
        QVERIFY(window.close());
    }
    void routesCloseAndThemeWithoutOwningWindow() {
        ZzFluentUI::ZzThemeController theme;
        theme.setMode(ZzFluentUI::ZzThemeMode::Light);
        CloseProbe window;
        {
            WindowChrome chrome(window, {theme});
            auto* title = qobject_cast<ZzFluentUI::ZzFluentTitleBar*>(window.menuWidget());
            QVERIFY(title);
            QSignalSpy requested(&chrome, &WindowChrome::themeModeRequested);
            Q_EMIT title->themeToggleRequested();
            QCOMPARE(requested.count(), 1);
            QCOMPARE(qvariant_cast<ZzFluentUI::ZzThemeMode>(requested.at(0).at(0)),
                     ZzFluentUI::ZzThemeMode::Dark);
            Q_EMIT title->closeRequested();
            QCOMPARE(window.closes, 1);
        }
        QCoreApplication::processEvents();
        QCOMPARE(window.closes, 1);
    }
};
QTEST_MAIN(WindowChromeTest)
#include "windowchrometest.moc"
