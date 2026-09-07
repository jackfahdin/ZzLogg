#include <QtTest>

#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>

#include "configuration.h"
#include "storagecontext.h"
#include "tabbedcrawlerwidget.h"

class TestCrawler final : public QWidget {
    Q_OBJECT
  Q_SIGNALS:
    void dataStatusChanged( DataStatus status );
};

class DocumentTabCloseTest final : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void initTestCase();
    void oneDocumentShowsCloseableTab();
    void closingOneOfTwoKeepsRemainingTabVisible();
    void destroyingTabsCancelsPendingIconRefresh();

  private:
    QTemporaryDir settingsRoot_;
};

void DocumentTabCloseTest::initTestCase()
{
    QVERIFY( settingsRoot_.isValid() );
    QVERIFY( StorageContext::install(
        { StorageMode::CustomDirectory, settingsRoot_.path(),
          settingsRoot_.filePath( QStringLiteral( "storage.ini" ) ), true } ) );
    Configuration::getSynced();
}

void DocumentTabCloseTest::oneDocumentShowsCloseableTab()
{
    TabbedCrawlerWidget tabs;
    tabs.setTabsClosable( true );
    tabs.show();

    auto* crawler = new TestCrawler;
    tabs.addCrawler( crawler, QStringLiteral( "single.log" ) );
    auto* tabBar = tabs.findChild<CrawlerTabBar*>();
    QVERIFY( tabBar != nullptr );
    QTRY_VERIFY( tabBar->isVisible() );

    QWidget* closeButton = tabBar->tabButton( 0, QTabBar::RightSide );
    if ( closeButton == nullptr ) {
        closeButton = tabBar->tabButton( 0, QTabBar::LeftSide );
    }
    QVERIFY( closeButton != nullptr );

    connect( &tabs, &QTabWidget::tabCloseRequested, &tabs,
             [ &tabs ]( int index ) { tabs.removeCrawler( index ); } );
    QTest::mouseClick( closeButton, Qt::LeftButton );

    QTRY_COMPARE( tabs.count(), 0 );
    QTRY_VERIFY( !tabBar->isVisible() );
}

void DocumentTabCloseTest::closingOneOfTwoKeepsRemainingTabVisible()
{
    TabbedCrawlerWidget tabs;
    tabs.setTabsClosable( true );
    tabs.show();
    tabs.addCrawler( new TestCrawler, QStringLiteral( "first.log" ) );
    tabs.addCrawler( new TestCrawler, QStringLiteral( "second.log" ) );

    auto* tabBar = tabs.findChild<CrawlerTabBar*>();
    QVERIFY( tabBar != nullptr );
    tabs.removeCrawler( 0 );

    QCOMPARE( tabs.count(), 1 );
    QTRY_VERIFY( tabBar->isVisible() );
}

void DocumentTabCloseTest::destroyingTabsCancelsPendingIconRefresh()
{
    // A queued icon refresh must not dereference a tab widget destroyed before delivery.
    auto* tabs = new TabbedCrawlerWidget;
    QEvent paletteChange{ QEvent::PaletteChange };
    QCoreApplication::sendEvent( tabs, &paletteChange );
    delete tabs;
    bool queueDrained = false;
    QMetaObject::invokeMethod( qApp, [ &queueDrained ] { queueDrained = true; },
                               Qt::QueuedConnection );
    QCoreApplication::sendPostedEvents( nullptr, QEvent::MetaCall );
    QVERIFY( queueDrained );
}

QTEST_MAIN( DocumentTabCloseTest )
#include "documenttabclosetest.moc"
