#include <QtTest>

#include <QAction>
#include <QPointer>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "configuration.h"
#include "filteredview.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "session.h"
#include "storagecontext.h"
#include "tabbedcrawlerwidget.h"

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
    QVERIFY( !firstLog.isEmpty() );
    QVERIFY( !secondLog.isEmpty() );

    auto session = std::make_shared<Session>();
    MainWindow window{ WindowSession{ session, QStringLiteral( "line-number-ui" ), 0 } };
    window.show();
    window.loadFileNonInteractive( firstLog );

    auto* action = window.findChild<QAction*>( QStringLiteral( "lineNumbersVisibleAction" ) );
    QVERIFY( action );
    QCOMPARE( action->text(), QStringLiteral( "Line &numbers" ) );
    QCOMPARE( window.findChildren<QAction*>( QRegularExpression{
                  QStringLiteral( "lineNumbersVisible(InMain|InFiltered)Action" ) } )
                  .size(),
              0 );

    auto* mainView = window.findChild<LogMainView*>( QStringLiteral( "logMainView" ) );
    auto* filteredView = window.findChild<FilteredView*>( QStringLiteral( "logFilteredView" ) );
    QTRY_VERIFY( mainView && filteredView );

    action->setChecked( false );
    QTRY_VERIFY( !mainView->lineNumbersVisible() );
    QTRY_VERIFY( !filteredView->lineNumbersVisible() );
    action->setChecked( true );
    QTRY_VERIFY( mainView->lineNumbersVisible() );
    QTRY_VERIFY( filteredView->lineNumbersVisible() );

    auto* tabs = window.findChild<TabbedCrawlerWidget*>( QStringLiteral( "documentTabs" ) );
    QVERIFY( tabs );
    QPointer<LogMainView> closedMainView{ mainView };
    QPointer<FilteredView> closedFilteredView{ filteredView };
    tabs->tabCloseRequested( 0 );
    QTRY_COMPARE( tabs->count(), 0 );
    QTRY_VERIFY( closedMainView.isNull() && closedFilteredView.isNull() );

    action->setChecked( false );
    window.loadFileNonInteractive( secondLog );
    mainView = nullptr;
    filteredView = nullptr;
    QTRY_VERIFY( ( mainView = window.findChild<LogMainView*>( QStringLiteral( "logMainView" ) ) )
                  && ( filteredView
                       = window.findChild<FilteredView*>( QStringLiteral( "logFilteredView" ) ) ) );
    QTRY_VERIFY( !mainView->lineNumbersVisible() );
    QTRY_VERIFY( !filteredView->lineNumbersVisible() );

    tabs->tabCloseRequested( 0 );
    QTRY_COMPARE( tabs->count(), 0 );
}

QTEST_MAIN( LineNumberUiTest )

#include "linenumberuitest.moc"
