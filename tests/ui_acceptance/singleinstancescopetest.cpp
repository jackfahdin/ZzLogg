/*
 * Copyright (C) 2016 -- 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// Real-process verification of directory-scoped single-instance naming:
// same-directory children (any path casing) become secondaries of the primary
// in that directory, while a different installation directory stays an
// independent primary. Key-string comparison alone would not prove the IPC
// behaviour, so every check below goes through real child processes.

#include "applicationupdateguard.h"

#include <kdsingleapplication.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class SingleInstanceScopeTest final : public QObject {
    Q_OBJECT

    QTemporaryDir root_;
    QString dirA_;
    QString dirB_;

    int runInstanceChild( const QString& directory )
    {
        QProcess child;
        child.start( QCoreApplication::applicationFilePath(),
                     { QStringLiteral( "--instance" ), directory } );
        if ( !child.waitForFinished( 15000 ) ) {
            child.kill();
            child.waitForFinished( 5000 );
            return -1;
        }
        return child.exitCode();
    }

  private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY( root_.isValid() );
        dirA_ = root_.filePath( QStringLiteral( "install" ) );
        dirB_ = root_.filePath( QStringLiteral( "other" ) );
        QVERIFY( QDir().mkpath( dirA_ ) );
        QVERIFY( QDir().mkpath( dirB_ ) );
    }

    void directoryScopedNaming()
    {
        const auto appFilePath = QCoreApplication::applicationFilePath();
        const auto legacyName = QFileInfo( appFilePath ).fileName();
        const auto nameA
            = ApplicationUpdateGuard::singleInstanceName( dirA_, appFilePath );
        const auto aliasA = QDir::toNativeSeparators( dirA_ ).toUpper();
        const auto nameAlias = ApplicationUpdateGuard::singleInstanceName( aliasA, appFilePath );
        const auto nameB
            = ApplicationUpdateGuard::singleInstanceName( dirB_, appFilePath );
        QVERIFY( nameA.startsWith( legacyName + QLatin1Char( '-' ) ) );
        QCOMPARE( nameAlias, nameA );
        QVERIFY( nameB != nameA );
        QVERIFY( nameB.startsWith( legacyName + QLatin1Char( '-' ) ) );
        // Unsupported or unverifiable directories keep the legacy name.
        QCOMPARE( ApplicationUpdateGuard::singleInstanceName(
                      root_.filePath( QStringLiteral( "missing" ) ), appFilePath ),
                  legacyName );
        QCOMPARE( ApplicationUpdateGuard::singleInstanceName(
                      QStringLiteral( "\\\\localhost\\C$\\nonexistent" ), appFilePath ),
                  legacyName );
    }

    void realProcessCoordination()
    {
        const auto appFilePath = QCoreApplication::applicationFilePath();
        const auto aliasA = QDir::toNativeSeparators( dirA_ ).toUpper();
        {
            KDSingleApplication primary{
                ApplicationUpdateGuard::singleInstanceName( dirA_, appFilePath )
            };
            QVERIFY( primary.isPrimaryInstance() );
            QCOMPARE( runInstanceChild( dirA_ ), 10 );
            QCOMPARE( runInstanceChild( aliasA ), 10 );
            QCOMPARE( runInstanceChild( dirB_ ), 0 );
        }
        QCOMPARE( runInstanceChild( dirA_ ), 0 );
    }
};

int main( int argc, char** argv )
{
    QCoreApplication app( argc, argv );
    const auto arguments = QCoreApplication::arguments();
    if ( arguments.size() >= 3 && arguments.at( 1 ) == QLatin1String( "--instance" ) ) {
        KDSingleApplication instance( ApplicationUpdateGuard::singleInstanceName(
            arguments.at( 2 ), QCoreApplication::applicationFilePath() ) );
        return instance.isPrimaryInstance() ? 0 : 10;
    }
    SingleInstanceScopeTest test;
    return QTest::qExec( &test, argc, argv );
}

#include "singleinstancescopetest.moc"
