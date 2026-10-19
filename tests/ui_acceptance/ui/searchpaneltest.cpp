#include "searchpanel.h"
#include "configuration.h"
#include "storagecontext.h"
#include <QLineEdit>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>

class SearchPanelTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY( settings_.isValid() );
        QVERIFY( StorageContext::install( { StorageMode::CustomDirectory, settings_.path(),
                                            settings_.filePath( "storage.ini" ), true } ) );
        Configuration::getSynced();
    }
    void emitsSearchAndStopIntents()
    {
        SearchPanel panel( { "previous" } );
        panel.show();
        QSignalSpy searches( &panel, &SearchPanel::searchRequested );
        QSignalSpy stops( &panel, &SearchPanel::stopRequested );
        panel.searchLineEdit()->setEditText( "ERROR" );
        panel.searchLineEdit()->setFocus();
        QCOMPARE( panel.focusWidget(), panel.searchLineEdit() );
        QTest::keyClick( panel.searchLineEdit(), Qt::Key_Return );
        QCOMPARE( searches.count(), 1 );
        panel.searchButton()->click();
        QCOMPARE( searches.count(), 2 );
        panel.stopButton()->setEnabled( true );
        panel.stopButton()->click();
        QCOMPARE( stops.count(), 1 );
        panel.clearButton()->click();
        QCOMPARE( panel.searchLineEdit()->currentText(), QString{} );
        QCOMPARE( searches.count(), 2 );
    }
    void keepsEditedTextWhenHistoryChanges()
    {
        SearchPanel panel( { "old" } );
        panel.searchLineEdit()->setEditText( "unfinished expression" );
        panel.updateHistory( { "new", "another" } );
        QCOMPARE( panel.searchLineEdit()->currentText(),
                  QStringLiteral( "unfinished expression" ) );
        QCOMPARE( panel.searchLineEdit()->itemText( 0 ), QStringLiteral( "new" ) );
        QCOMPARE( panel.searchLineEdit()->count(), 2 );
    }
    void exposesOptionsWithoutPerformingSearch()
    {
        SearchPanel panel( {} );
        QSignalSpy searches( &panel, &SearchPanel::searchRequested );
        QSignalSpy changed( &panel, &SearchPanel::matchCaseChanged );
        const bool before = panel.matchCaseButton()->isChecked();
        panel.matchCaseButton()->click();
        QCOMPARE( changed.count(), 1 );
        QCOMPARE( changed.at( 0 ).at( 0 ).toBool(), !before );
        QCOMPARE( searches.count(), 0 );
    }
    void destroysOwnedContextMenu()
    {
        QPointer<QMenu> menu;
        {
            SearchPanel panel( {} );
            menu = panel.searchLineContextMenu();
            QVERIFY( menu );
            QCOMPARE( menu->windowType(), Qt::Popup );
        }
        QVERIFY( menu.isNull() );
    }

private:
    QTemporaryDir settings_;
};
QTEST_MAIN( SearchPanelTest )
#include "searchpaneltest.moc"
