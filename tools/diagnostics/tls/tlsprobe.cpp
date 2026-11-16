#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {
QJsonArray loadedModules()
{
    QJsonArray modules;
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                                     GetCurrentProcessId() );
    if ( snapshot == INVALID_HANDLE_VALUE )
        return modules;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof( entry );
    if ( Module32FirstW( snapshot, &entry ) ) {
        do {
            modules.append( QString::fromWCharArray( entry.szExePath ) );
        } while ( Module32NextW( snapshot, &entry ) );
    }
    CloseHandle( snapshot );
#endif
    return modules;
}
}

int main( int argc, char** argv )
{
    QCoreApplication app( argc, argv );
    QCommandLineParser parser;
    parser.setApplicationDescription( "Explicit TLS diagnostic; does not exercise application download policies." );
    parser.addHelpOption();
    parser.addOptions( {
        { "inspect", "Report TLS capability without a request." },
        { "backend", "Select a Qt TLS backend before creating network objects.", "name" },
        { "isolated-plugins", "Search plugins only beside this executable." },
        { "url", "HTTPS URL to fetch.", "url" },
        { "sha256", "Required SHA-256 of a successful HTTP 200 response.", "hex" },
        { "expect-tls-error", "Pass only on a certificate error and failed TLS handshake." },
        { "ca-file", "Additional PEM CA certificates for this request only.", "file" },
        { "proxy", "system, direct, or http://host:port (no authentication).", "proxy", "system" },
        { "timeout-ms", "Total request timeout (1..120000 ms).", "ms", "15000" }
    } );
    parser.process( app );
    if ( parser.isSet( "isolated-plugins" ) )
        QCoreApplication::setLibraryPaths( { QCoreApplication::applicationDirPath() } );

    QJsonObject report{
        { "qt_version", qVersion() },
        { "qt_build_version", QT_VERSION_STR },
        { "os", QSysInfo::prettyProductName() },
        { "available_backends", QJsonArray::fromStringList( QSslSocket::availableBackends() ) },
        { "plugin_paths", QJsonArray::fromStringList( QCoreApplication::libraryPaths() ) }
    };
    auto finish = [ & ]( bool passed, const QString& reason ) {
        report["passed"] = passed;
        report["reason"] = reason;
        report["active_backend"] = QSslSocket::activeBackend();
        report["ssl_runtime_version"] = QSslSocket::sslLibraryVersionString();
        report["ssl_build_version"] = QSslSocket::sslLibraryBuildVersionString();
        report["loaded_modules"] = loadedModules();
        QTextStream( stdout ) << QJsonDocument( report ).toJson( QJsonDocument::Indented );
        return passed ? 0 : 1;
    };
    if ( parser.isSet( "backend" ) && !QSslSocket::setActiveBackend( parser.value( "backend" ) ) )
        return finish( false, "Requested TLS backend is unavailable" );
    report["supports_ssl"] = QSslSocket::supportsSsl();
    if ( !QSslSocket::supportsSsl() )
        return finish( false, "TLS is unavailable" );
    if ( parser.isSet( "inspect" ) )
        return finish( true, "Capability only; no HTTPS request performed" );

    const QUrl url( parser.value( "url" ), QUrl::StrictMode );
    const auto expectedHash = parser.value( "sha256" ).toLower();
    const bool negative = parser.isSet( "expect-tls-error" );
    if ( !url.isValid() || url.scheme() != "https" || url.host().isEmpty()
         || !url.userInfo().isEmpty() || url.hasFragment() )
        return finish( false, "An HTTPS URL without credentials or fragment is required" );
    if ( negative ? parser.isSet( "sha256" )
                  : !QRegularExpression( "^[0-9a-f]{64}$" ).match( expectedHash ).hasMatch() )
        return finish( false, "Choose either --sha256 <64 hex digits> or --expect-tls-error" );
    bool validTimeout = false;
    const int timeoutMs = parser.value( "timeout-ms" ).toInt( &validTimeout );
    if ( !validTimeout || timeoutMs < 1 || timeoutMs > 120000 )
        return finish( false, "Invalid timeout" );

    QNetworkAccessManager manager;
    const auto proxy = parser.value( "proxy" );
    report["proxy"] = proxy;
    if ( proxy == "system" )
        QNetworkProxyFactory::setUseSystemConfiguration( true );
    else if ( proxy == "direct" )
        manager.setProxy( QNetworkProxy::NoProxy );
    else {
        const QUrl proxyUrl( proxy, QUrl::StrictMode );
        if ( !proxyUrl.isValid() || proxyUrl.scheme() != "http" || proxyUrl.host().isEmpty()
             || proxyUrl.port() < 1 || proxyUrl.port() > 65535 || !proxyUrl.userInfo().isEmpty()
             || !proxyUrl.path().isEmpty() || proxyUrl.hasQuery() || proxyUrl.hasFragment() )
            return finish( false, "Expected http://host:port proxy without credentials or path" );
        manager.setProxy( QNetworkProxy( QNetworkProxy::HttpProxy, proxyUrl.host(),
                                        static_cast<quint16>( proxyUrl.port() ) ) );
    }
    QNetworkRequest request( url );
    request.setAttribute( QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy );
    request.setMaximumRedirectsAllowed( 5 );
    request.setRawHeader( "Accept-Encoding", "identity" );
    auto ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode( QSslSocket::VerifyPeer );
    if ( parser.isSet( "ca-file" ) ) {
        QFile file( parser.value( "ca-file" ) );
        if ( !file.open( QIODevice::ReadOnly ) )
            return finish( false, "Cannot read CA file" );
        const auto certificates = QSslCertificate::fromData( file.readAll() );
        if ( certificates.isEmpty() )
            return finish( false, "CA file has no PEM certificates" );
        ssl.addCaCertificates( certificates );
    }
    request.setSslConfiguration( ssl );
    auto* reply = manager.get( request );
    reply->setReadBufferSize( 128 * 1024 );
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot( true );
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    qint64 bytes = 0;
    bool encrypted = false;
    bool timedOut = false;
    QJsonArray sslErrors;
    auto consume = [ & ] {
        while ( reply->bytesAvailable() > 0 ) {
            const auto chunk = reply->read( 64 * 1024 );
            if ( chunk.isEmpty() )
                break;
            bytes += chunk.size();
            hash.addData( chunk );
        }
    };
    QObject::connect( reply, &QNetworkReply::readyRead, &loop, consume );
    QObject::connect( reply, &QNetworkReply::encrypted, &loop, [ & ] { encrypted = true; } );
    QObject::connect( reply, &QNetworkReply::sslErrors, &loop, [ & ]( const QList<QSslError>& errors ) {
        for ( const auto& error : errors )
            sslErrors.append( error.errorString() );
        // Never ignore errors; the negative case must observe a failed handshake.
    } );
    QObject::connect( reply, &QNetworkReply::finished, &loop, &QEventLoop::quit );
    QObject::connect( &timer, &QTimer::timeout, &loop, [ & ] {
        timedOut = true;
        reply->abort();
        loop.quit();
    } );
    timer.start( timeoutMs );
    if ( !reply->isFinished() )
        loop.exec();
    timer.stop();
    consume();
    const auto actualHash = QString::fromLatin1( hash.result().toHex() );
    const auto status = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
    report["url"] = url.toString();
    report["final_url"] = reply->url().toString();
    report["http_status"] = status;
    report["network_error"] = static_cast<int>( reply->error() );
    report["network_error_string"] = reply->errorString();
    report["encrypted"] = encrypted;
    report["ssl_errors"] = sslErrors;
    report["timed_out"] = timedOut;
    report["bytes"] = bytes;
    report["sha256"] = actualHash;
    report["expected_tls_error"] = negative;
    const bool passed = !timedOut && ( negative
        ? !sslErrors.isEmpty() && reply->error() == QNetworkReply::SslHandshakeFailedError
        : encrypted && sslErrors.isEmpty() && reply->error() == QNetworkReply::NoError
            && status == 200 && reply->url().scheme() == "https" && actualHash == expectedHash );
    return finish( passed, passed ? "Expected transport result observed" : "Transport expectation failed" );
}
