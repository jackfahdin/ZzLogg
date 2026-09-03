#include <QtTest>

#include <algorithm>
#include <utility>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "configuration.h"
#include "crawlerwidget.h"
#include "filteredview.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "persistentinfo.h"
#include "session.h"
#include "storagecontext.h"
#include "tabbedcrawlerwidget.h"

struct LineNumberCrawlerAccess {
};

template <>
struct CrawlerWidget::access_by<LineNumberCrawlerAccess> {
    static bool loadingFinished( const CrawlerWidget& crawler )
    {
        return !crawler.loadingInProgress_;
    }
};

using LineCrawlerAccess = CrawlerWidget::access_by<LineNumberCrawlerAccess>;

class LineNumberUiTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void initTestCase();
    void oneActionSynchronizesLineNumbersAcrossViewsAndDocuments();

  private:
    QString createLogFile( QTemporaryDir& directory, const QString& name ) const;
    QTemporaryDir settingsRoot_;
};

void LineNumberUiTest::initTestCase()
{
    QVERIFY( settingsRoot_.isValid() );
    QVERIFY( StorageContext::install(
        { StorageMode::CustomDirectory, settingsRoot_.path(),
          settingsRoot_.filePath( QStringLiteral( "storage.ini" ) ), true } ) );
    auto& config = Configuration::getSynced();
    config.setLineNumbersVisible( true );
    config.save();
}

QString LineNumberUiTest::createLogFile( QTemporaryDir& directory, const QString& name ) const
{
    const QString path = directory.filePath( name );
    QFile file{ path };
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
        return {};
    }
    file.write( "first line\nsecond line\n" );
    return path;
}

void LineNumberUiTest::oneActionSynchronizesLineNumbersAcrossViewsAndDocuments()
{
    QTemporaryDir logs;
    QVERIFY( logs.isValid() );
    const QString firstLog = createLogFile( logs, QStringLiteral( "first.log" ) );
    const QString secondLog = createLogFile( logs, QStringLiteral( "second.log" ) );
    const QString thirdLog = createLogFile( logs, QStringLiteral( "third.log" ) );
    QVERIFY( !firstLog.isEmpty() );
    QVERIFY( !secondLog.isEmpty() );
    QVERIFY( !thirdLog.isEmpty() );

    auto session = std::make_shared<Session>();
    MainWindow window{ WindowSession{ session, QStringLiteral( "line-number-ui" ), 0 } };
    window.show();
    window.loadFileNonInteractive( firstLog );

    auto* action = window.findChild<QAction*>( QStringLiteral( "lineNumbersVisibleAction" ) );
    QVERIFY( action );
    QCOMPARE( action->text(), QStringLiteral( "Line &numbers" ) );
    QMenu* viewMenu = nullptr;
    const auto menus = window.menuBar()->findChildren<QMenu*>( QString(), Qt::FindDirectChildrenOnly );
    for ( auto* menu : menus ) {
        if ( menu->title() == QStringLiteral( "&View" ) ) {
            viewMenu = menu;
            break;
        }
    }
    QVERIFY( viewMenu );

    QList<QAction*> lineNumberActions;
    for ( auto* candidate : viewMenu->actions() ) {
        if ( candidate->text().contains( QStringLiteral( "line" ), Qt::CaseInsensitive )
             && candidate->text().contains( QStringLiteral( "number" ), Qt::CaseInsensitive ) ) {
            lineNumberActions.append( candidate );
        }
    }
    QCOMPARE( lineNumberActions.size(), 1 );
    QCOMPARE( lineNumberActions.constFirst(), action );

    auto* tabs = window.findChild<TabbedCrawlerWidget*>( QStringLiteral( "documentTabs" ) );
    QVERIFY( tabs );
    window.loadFileNonInteractive( secondLog );
    QTRY_COMPARE( tabs->count(), 2 );
    QTRY_VERIFY_WITH_TIMEOUT(
        ( [tabs] {
            for ( int index = 0; index < tabs->count(); ++index ) {
                const auto* crawler = qobject_cast<CrawlerWidget*>( tabs->widget( index ) );
                if ( crawler == nullptr || !LineCrawlerAccess::loadingFinished( *crawler ) ) {
                    return false;
                }
            }
            return true;
        } )(),
        5000 );

    tabs->setCurrentIndex( 0 );
    auto* const firstCrawler = qobject_cast<CrawlerWidget*>( tabs->currentWidget() );
    QVERIFY( firstCrawler );
    auto* const searchEdit
        = firstCrawler->findChild<QComboBox*>( QStringLiteral( "mainSearchEdit" ) );
    auto* const searchButton
        = firstCrawler->findChild<QToolButton*>( QStringLiteral( "mainSearchButton" ) );
    auto* const keepResultsButton
        = firstCrawler->findChild<QToolButton*>( QStringLiteral( "keepSearchResultsButton" ) );
    auto* const filteredResultsTabs
        = firstCrawler->findChild<QTabWidget*>( QStringLiteral( "filteredResultsTabs" ) );
    QVERIFY( searchEdit );
    QVERIFY( searchButton );
    QVERIFY( keepResultsButton );
    QVERIFY( filteredResultsTabs );

    searchEdit->setCurrentText( QStringLiteral( "first" ) );
    searchButton->click();
    QTRY_VERIFY_WITH_TIMEOUT( searchButton->isVisible(), 5000 );
    QCOMPARE( filteredResultsTabs->count(), 1 );

    keepResultsButton->setChecked( true );
    searchEdit->setCurrentText( QStringLiteral( "second" ) );
    searchButton->click();
    QTRY_VERIFY_WITH_TIMEOUT( searchButton->isVisible(), 5000 );
    QTRY_COMPARE( filteredResultsTabs->count(), 2 );

    keepResultsButton->setChecked( true );
    searchEdit->setCurrentText( QStringLiteral( "line" ) );
    searchButton->click();
    QTRY_VERIFY_WITH_TIMEOUT( searchButton->isVisible(), 5000 );
    QTRY_COMPARE( filteredResultsTabs->count(), 3 );
    const auto firstCrawlerFilteredViews = firstCrawler->findChildren<FilteredView*>();
    QCOMPARE( firstCrawlerFilteredViews.size(), 3 );

    auto mainViews = window.findChildren<LogMainView*>( QStringLiteral( "logMainView" ) );
    auto filteredViews = window.findChildren<FilteredView*>();
    QCOMPARE( mainViews.size(), 2 );
    QCOMPARE( filteredViews.size(), 4 );
    QVERIFY( std::all_of( mainViews.cbegin(), mainViews.cend(),
                          []( const auto* view ) { return view->lineNumbersVisible(); } ) );
    QVERIFY( std::all_of( filteredViews.cbegin(), filteredViews.cend(),
                          []( const auto* view ) { return view->lineNumbersVisible(); } ) );
    QVERIFY( action->isChecked() );

    for ( int index = 0; index < tabs->count(); ++index ) {
        tabs->setCurrentIndex( index );
        QCoreApplication::processEvents();
        QVERIFY( action->isChecked() );
    }

    action->setChecked( false );
    QTRY_VERIFY( std::all_of( mainViews.cbegin(), mainViews.cend(),
                              []( const auto* view ) { return !view->lineNumbersVisible(); } ) );
    QTRY_VERIFY( std::all_of(
        filteredViews.cbegin(), filteredViews.cend(),
        []( const auto* view ) { return !view->lineNumbersVisible(); } ) );
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.sync();
    QCOMPARE( settings.value( QStringLiteral( "view.lineNumbersVisible" ) ).toBool(), false );

    action->setChecked( true );
    QTRY_VERIFY( std::all_of( mainViews.cbegin(), mainViews.cend(),
                              []( const auto* view ) { return view->lineNumbersVisible(); } ) );
    QTRY_VERIFY( std::all_of( filteredViews.cbegin(), filteredViews.cend(),
                              []( const auto* view ) { return view->lineNumbersVisible(); } ) );
    settings.sync();
    QCOMPARE( settings.value( QStringLiteral( "view.lineNumbersVisible" ) ).toBool(), true );

    QList<QPointer<LogMainView>> closedMainViews;
    QList<QPointer<FilteredView>> closedFilteredViews;
    for ( auto* view : std::as_const( mainViews ) ) {
        closedMainViews.append( view );
    }
    for ( auto* view : std::as_const( filteredViews ) ) {
        closedFilteredViews.append( view );
    }
    tabs->tabCloseRequested( 1 );
    tabs->tabCloseRequested( 0 );
    QTRY_COMPARE( tabs->count(), 0 );
    QTRY_VERIFY( std::all_of( closedMainViews.cbegin(), closedMainViews.cend(),
                              []( const auto& view ) { return view.isNull(); } ) );
    QTRY_VERIFY( std::all_of( closedFilteredViews.cbegin(), closedFilteredViews.cend(),
                              []( const auto& view ) { return view.isNull(); } ) );

    action->setChecked( false );
    window.loadFileNonInteractive( thirdLog );
    LogMainView* mainView = nullptr;
    FilteredView* filteredView = nullptr;
    QTRY_VERIFY(
        ( mainView = window.findChild<LogMainView*>( QStringLiteral( "logMainView" ) ) )
        && ( filteredView
             = window.findChild<FilteredView*>( QStringLiteral( "logFilteredView" ) ) ) );
    QTRY_VERIFY( !mainView->lineNumbersVisible() );
    QTRY_VERIFY( !filteredView->lineNumbersVisible() );
    settings.sync();
    QCOMPARE( settings.value( QStringLiteral( "view.lineNumbersVisible" ) ).toBool(), false );

    tabs->tabCloseRequested( 0 );
    QTRY_COMPARE( tabs->count(), 0 );
}

QTEST_MAIN( LineNumberUiTest )

#include "linenumberuitest.moc"
