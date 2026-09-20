#include "codesyntax.h"
#include <QSignalSpy>
#include <QtTest>

class SyntaxHighlightTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void detection()
    {
        CodeSyntax syntax( []( quint64 ) { return QString{}; }, [] { return quint64{ 0 }; } );
        syntax.setFileName( "example.cpp" );
        QCOMPARE( syntax.definitionName(), QString( "C++" ) );
        syntax.setFileName( "example.java" );
        QCOMPARE( syntax.definitionName(), QString( "Java" ) );
        syntax.setFileName( "example.json" );
        QCOMPARE( syntax.definitionName(), QString( "JSON" ) );
        syntax.setFileName( "example.log" );
        QVERIFY( syntax.definitionName().isEmpty() );
        syntax.setLanguage( "cpp" );
        QCOMPARE( syntax.definitionName(), QString( "C++" ) );
        syntax.setLanguage( "plain" );
        QVERIFY( syntax.definitionName().isEmpty() );
    }
    void multilineAndInvalidation()
    {
        QStringList lines{ "/* comment", "int inside = 1;", "*/ int outside = 2;" };
        CodeSyntax syntax( [ & ]( quint64 i ) { return lines.at( qsizetype( i ) ); },
                           [ & ] { return quint64( lines.size() ); } );
        syntax.setLanguage( "cpp" );
        // Request only a filtered result: the opening comment is not visible.
        QTRY_VERIFY( !syntax.formats( 1, false ).isEmpty() );
        const auto comment = syntax.formats( 1, false ).front().color;
        QTRY_VERIFY( !syntax.formats( 2, false ).isEmpty() );
        QVERIFY( syntax.formats( 2, false ).back().color != comment );
        lines[ 0 ] = "int normal = 0;";
        syntax.invalidate();
        QTRY_VERIFY( !syntax.formats( 1, false ).isEmpty() );
        QVERIFY( syntax.formats( 1, false ).front().color != comment );
        const auto light = syntax.formats( 1, false ).front().color;
        QVERIFY( syntax.formats( 1, true ).front().color != light );
    }
    void languagesAndRawStrings()
    {
        QStringList lines{ "auto s = R\"tag(first", "int stillString;", ")tag\"; int n = 4;" };
        CodeSyntax syntax( [ & ]( quint64 i ) { return lines.at( qsizetype( i ) ); },
                           [ & ] { return quint64( lines.size() ); } );
        syntax.setLanguage( "cpp" );
        QTRY_VERIFY( !syntax.formats( 1, false ).isEmpty() );
        QCOMPARE( syntax.formats( 1, false ).size(), 1 );
        syntax.setLanguage( "java" );
        lines = { "public class Test { String s = \"hi\"; // comment" };
        syntax.invalidate();
        QTRY_VERIFY( syntax.formats( 0, false ).size() >= 3 );
        syntax.setLanguage( "json" );
        lines = { "{\"key\": true, \"n\": 42, \"s\": \"text\"}" };
        syntax.invalidate();
        QTRY_VERIFY( syntax.formats( 0, false ).size() >= 4 );
    }
    void longLineFallback()
    {
        QStringList lines{ QString( 20000, 'x' ) + "/*", "int mustRemainPlain;" };
        CodeSyntax syntax( [ & ]( quint64 i ) { return lines.at( qsizetype( i ) ); },
                           [ & ] { return quint64( lines.size() ); } );
        syntax.setLanguage( "cpp" );
        QSignalSpy changed( &syntax, &CodeSyntax::changed );
        QVERIFY( syntax.formats( 1, false ).isEmpty() );
        QTRY_VERIFY( changed.count() > 0 );
        QVERIFY( syntax.formats( 1, false ).isEmpty() );
        QCOMPARE( syntax.plainTextFrom(), quint64( 0 ) );
    }
    void denseLinesStayBoundedAndPreserveState()
    {
        QString dense;
        for ( int i = 0; i < 1000; ++i )
            dense += "0, ";
        QStringList lines{ dense + "/*", "int comment;", "*/ int code;" };
        CodeSyntax syntax( [ & ]( quint64 i ) { return lines.at( qsizetype( i ) ); },
                           [ & ] { return quint64( lines.size() ); } );
        syntax.setLanguage( "cpp" );
        QSignalSpy changed( &syntax, &CodeSyntax::changed );
        QVERIFY( syntax.formats( 0, false ).isEmpty() );
        QTRY_VERIFY( changed.count() > 0 );
        QVERIFY( syntax.formats( 0, false ).isEmpty() );
        QTRY_VERIFY( !syntax.formats( 1, false ).isEmpty() );
        QCOMPARE( syntax.formats( 1, false ).size(), 1 );
        const auto comment = syntax.formats( 1, false ).front().color;
        QTRY_VERIFY( !syntax.formats( 2, false ).isEmpty() );
        QVERIFY( syntax.formats( 2, false ).back().color != comment );
        const auto count = changed.count();
        for ( int i = 0; i < 100; ++i )
            syntax.formats( 0, false );
        QTest::qWait( 30 );
        QCOMPARE( changed.count(), count );
    }
    void refusesOversizedInputAndFarLines()
    {
        int reads = 0;
        CodeSyntax syntax(
            [ & ]( quint64 ) -> std::optional<QString> {
                ++reads;
                return std::nullopt;
            },
            [] { return quint64( 200000 ); } );
        syntax.setLanguage( "java" );
        QVERIFY( syntax.formats( 100000, false ).isEmpty() );
        QTest::qWait( 20 );
        QCOMPARE( reads, 0 );
        QSignalSpy changed( &syntax, &CodeSyntax::changed );
        syntax.formats( 5, false );
        QTRY_VERIFY( changed.count() > 0 );
        QCOMPARE( reads, 1 );
        QCOMPARE( syntax.plainTextFrom(), quint64( 0 ) );
    }
    void randomJump()
    {
        QStringList lines;
        for ( int i = 0; i < 5000; ++i )
            lines.append( "int n = 1;" );
        lines[ 1023 ] = "/*";
        lines[ 4500 ] = "*/";
        CodeSyntax syntax( [ & ]( quint64 i ) { return lines.at( qsizetype( i ) ); },
                           [ & ] { return quint64( lines.size() ); } );
        syntax.setLanguage( "cpp" );
        QTRY_VERIFY( !syntax.formats( 4400, false ).isEmpty() );
        const auto comment = syntax.formats( 4400, false ).front().color;
        QTRY_VERIFY( !syntax.formats( 2000, false ).isEmpty() );
        QCOMPARE( syntax.formats( 2000, false ).front().color, comment );
        QTRY_VERIFY( !syntax.formats( 4999, false ).isEmpty() );
        QVERIFY( syntax.formats( 4999, false ).front().color != comment );
    }
};
QTEST_GUILESS_MAIN( SyntaxHighlightTest )
#include "syntaxhighlighttest.moc"
