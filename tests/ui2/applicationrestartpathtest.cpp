#include "applicationrunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QString executableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral( "zzlogg-restart-probe.exe" );
#else
    return QStringLiteral( "zzlogg-restart-probe" );
#endif
}

bool createExecutable( const QString& path )
{
    if ( !QDir{}.mkpath( QFileInfo{ path }.absolutePath() ) ) {
        return false;
    }
    QFile file{ path };
    if ( !file.open( QIODevice::WriteOnly ) || file.write( "restart-probe" ) < 0 ) {
        return false;
    }
    file.close();
    return file.setPermissions( QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                | QFileDevice::ExeOther );
}

class ProcessEnvironmentGuard final {
  public:
    ProcessEnvironmentGuard()
        : path_{ qgetenv( "PATH" ) }
        , currentDirectory_{ QDir::currentPath() }
    {
    }

    ~ProcessEnvironmentGuard()
    {
        qputenv( "PATH", path_ );
        QDir::setCurrent( currentDirectory_ );
    }

  private:
    QByteArray path_;
    QString currentDirectory_;
};

} // namespace

class ApplicationRestartPathTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void resolvesBareExecutableFromPath()
    {
        ProcessEnvironmentGuard guard;
        QTemporaryDir root;
        QVERIFY( root.isValid() );
        const QString pathDirectory = root.filePath( QStringLiteral( "path-bin" ) );
        const QString executable = QDir{ pathDirectory }.filePath( executableName() );
        QVERIFY( createExecutable( executable ) );
        const QString unrelatedDirectory = root.filePath( QStringLiteral( "working" ) );
        QVERIFY( QDir{}.mkpath( unrelatedDirectory ) );
        QVERIFY( QDir::setCurrent( unrelatedDirectory ) );
        qputenv( "PATH", QDir::toNativeSeparators( pathDirectory ).toLocal8Bit() );

        const QString resolved = resolveRestartExecutablePath( executableName() );

        QCOMPARE( resolved, QFileInfo{ executable }.absoluteFilePath() );
        QVERIFY( !resolved.startsWith( unrelatedDirectory ) );
    }

    void resolvesExecutableWithRelativeDirectoryFromCurrentDirectory()
    {
        ProcessEnvironmentGuard guard;
        QTemporaryDir root;
        QVERIFY( root.isValid() );
        const QString workingDirectory = root.filePath( QStringLiteral( "working" ) );
        const QString relativePath
            = QDir{ QStringLiteral( "relative-bin" ) }.filePath( executableName() );
        const QString executable = QDir{ workingDirectory }.filePath( relativePath );
        QVERIFY( createExecutable( executable ) );
        QVERIFY( QDir::setCurrent( workingDirectory ) );

        const QString resolved = resolveRestartExecutablePath( relativePath );

        QCOMPARE( resolved, QFileInfo{ executable }.absoluteFilePath() );
    }
};

QTEST_APPLESS_MAIN( ApplicationRestartPathTest )
#include "applicationrestartpathtest.moc"
