#include <QtTest>

#include <QCloseEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>

#include <ZzCore/ZzError.h>
#include <ZzCore/ZzErrorCode.h>
#include <ZzFluentUI/ZzColorToken.h>
#include <ZzFluentUI/ZzFluentStyle.h>
#include <ZzFluentUI/ZzFluentTitleBar.h>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzThemeSnapshot.h>
#include <ZzFluentUI/ZzTitleBarMenuDisplayMode.h>
#include <ZzWindowKit/ZzWindowAgent.h>

#include "windowchrome.h"

namespace {

class CloseProbeWindow final : public QMainWindow {
public:
    int closeEventCount = 0;

protected:
    void closeEvent( QCloseEvent* event ) override
    {
        ++closeEventCount;
        event->ignore();
    }
};

class ApplicationStyleReset final {
public:
    ApplicationStyleReset()
        : style_( QApplication::style() )
        , palette_( QApplication::palette() )
    {
        if ( style_ != nullptr && style_->parent() == qApp ) {
            style_->setParent( nullptr );
        }
    }

    ~ApplicationStyleReset()
    {
        QApplication::setStyle( style_ );
        QApplication::setPalette( palette_ );
    }

private:
    QStyle* style_;
    QPalette palette_;
};

ZzFluentUI::ZzFluentTitleBar* titleBarFor( QMainWindow& window )
{
    return window.findChild<ZzFluentUI::ZzFluentTitleBar*>(
        QStringLiteral( "zzloggFluentTitleBar" ) );
}

} // namespace

void verifyTitleFormatting()
{
    QCOMPARE( formatWindowTitle( {} ), QStringLiteral( "ZzLogg" ) );
    QCOMPARE( formatWindowTitle( QStringLiteral( "server.log" ) ),
              QStringLiteral( "server.log \u2014 ZzLogg" ) );
    QCOMPARE( formatWindowTitle( QStringLiteral( "\u670d\u52a1\u5668-\U0001f680.log" ) ),
              QStringLiteral( "\u670d\u52a1\u5668-\U0001f680.log \u2014 ZzLogg" ) );

    const QString longName( 512, QLatin1Char( 'x' ) );
    const QString formatted = formatWindowTitle( longName );
    QCOMPARE( formatted, longName + QStringLiteral( " \u2014 ZzLogg" ) );
    QCOMPARE( formatted.size(), 521 );
}

void verifyActiveDocumentTitleSynchronization()
{
    QMainWindow window;
    ZzFluentUI::ZzThemeController theme;
    WindowChrome installed(window, {theme});
    QVERIFY(!installed.usesNativeFallback());
    auto* titleBar = titleBarFor( window );
    QVERIFY( titleBar );

    QCOMPARE( window.windowTitle(), QStringLiteral( "ZzLogg" ) );
    QCOMPARE( titleBar->title(), QStringLiteral( "ZzLogg" ) );

    installed.setDocumentName( QStringLiteral( "\u65e5\u5fd7-\U0001f680.log" ) );
    QCOMPARE( window.windowTitle(), QStringLiteral( "\u65e5\u5fd7-\U0001f680.log \u2014 ZzLogg" ) );
    QCOMPARE( titleBar->title(), window.windowTitle() );

    const QString longName( 512, QLatin1Char( 'L' ) );
    installed.setDocumentName( longName );
    QCOMPARE( window.windowTitle(), longName + QStringLiteral( " \u2014 ZzLogg" ) );
    QCOMPARE( titleBar->title(), window.windowTitle() );
    QCOMPARE( titleBar->title().size(), 521 );
}

void verifyChromeStateAndIconSynchronization()
{
    QMainWindow window;
    window.resize( 640, 480 );
    window.move( 100, 100 );
    ZzFluentUI::ZzThemeController theme;
    WindowChrome installed(window, {theme});
    QVERIFY(!installed.usesNativeFallback());
    auto* titleBar = titleBarFor( window );
    QVERIFY( titleBar );

    const QList<QWidget*> hitTestWidgets = titleBar->hitTestVisibleWidgets();
    QVERIFY( hitTestWidgets.contains( titleBar->menuBar() ) );
    QVERIFY( !hitTestWidgets.contains( titleBar ) );
    for ( QWidget* widget : hitTestWidgets ) {
        QVERIFY( widget );
        QVERIFY( titleBar->isAncestorOf( widget ) );
    }
#ifdef Q_OS_WIN
    QVERIFY( !titleBar->minimizeButton()->isHidden() );
    QVERIFY( !titleBar->maximizeButton()->isHidden() );
    QVERIFY( !titleBar->closeButton()->isHidden() );
#endif

    QPixmap iconPixmap( 16, 16 );
    iconPixmap.fill( QColor( 220, 30, 40 ) );
    window.setWindowIcon( QIcon( iconPixmap ) );
    QCoreApplication::processEvents();
    auto* iconLabel = qobject_cast<QLabel*>( titleBar->windowIconWidget() );
    QVERIFY( iconLabel );
    const QImage titleIcon = iconLabel->pixmap().toImage();
    QVERIFY( !titleIcon.isNull() );
    QCOMPARE( titleIcon.pixelColor( titleIcon.width() / 2, titleIcon.height() / 2 ),
              QColor( 220, 30, 40 ) );

    window.showMaximized();
    QTRY_VERIFY( window.isMaximized() );
    QTRY_COMPARE( titleBar->maximizeButton()->accessibleName(), QStringLiteral( "\u8fd8\u539f" ) );
    window.showNormal();
    QTRY_VERIFY( !window.isMaximized() );
    QTRY_COMPARE( titleBar->maximizeButton()->accessibleName(),
                  QStringLiteral( "\u6700\u5927\u5316" ) );
    window.close();
}

void verifyWindowButtonIntents()
{
    CloseProbeWindow window;
    window.resize( 640, 480 );
    window.move( 100, 100 );
    ZzFluentUI::ZzThemeController theme;
    WindowChrome installed(window, {theme});
    QVERIFY(!installed.usesNativeFallback());
    auto* titleBar = titleBarFor( window );
    QVERIFY( titleBar );
    window.show();
    QTRY_VERIFY( window.isVisible() );

    QVERIFY( QMetaObject::invokeMethod( titleBar, "minimizeRequested" ) );
    QTRY_VERIFY( window.isMinimized() );
    window.showNormal();
    QTRY_VERIFY( !window.isMinimized() );

    QVERIFY( QMetaObject::invokeMethod( titleBar, "maximizeRestoreRequested" ) );
    QTRY_VERIFY( window.isMaximized() );
    QVERIFY( QMetaObject::invokeMethod( titleBar, "maximizeRestoreRequested" ) );
    QTRY_VERIFY( !window.isMaximized() );

    QCOMPARE( window.closeEventCount, 0 );
    QVERIFY( QMetaObject::invokeMethod( titleBar, "closeRequested" ) );
    QCOMPARE( window.closeEventCount, 1 );
    window.hide();
}

void verifyAlwaysOnTopPreservesWindowPresentation()
{
    QMainWindow window;
    window.resize( 640, 480 );
    window.move( 100, 100 );
    ZzFluentUI::ZzThemeController theme;
    WindowChrome installed(window, {theme});
    QVERIFY(!installed.usesNativeFallback());
    auto* titleBar = titleBarFor( window );
    QVERIFY( titleBar );

    window.setWindowState( Qt::WindowMinimized );
    const auto hiddenState = window.windowState();
    QVERIFY( !window.isVisible() );
    QVERIFY( QMetaObject::invokeMethod( titleBar, "alwaysOnTopRequested", Q_ARG( bool, true ) ) );
    QVERIFY( !window.isVisible() );
    QCOMPARE( window.windowState(), hiddenState );
    QVERIFY( window.windowFlags().testFlag( Qt::WindowStaysOnTopHint ) );
    QVERIFY( titleBar->isAlwaysOnTop() );

    window.showMaximized();
    QTRY_VERIFY( window.isVisible() );
    QTRY_VERIFY( window.isMaximized() );
    const auto visibleState = window.windowState();
    QVERIFY( QMetaObject::invokeMethod( titleBar, "alwaysOnTopRequested", Q_ARG( bool, false ) ) );
    QVERIFY( window.isVisible() );
    QCOMPARE( window.windowState(), visibleState );
    QVERIFY( !window.windowFlags().testFlag( Qt::WindowStaysOnTopHint ) );
    QVERIFY( !titleBar->isAlwaysOnTop() );
    window.close();
}
