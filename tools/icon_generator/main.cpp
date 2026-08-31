#include <QBuffer>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QMap>
#include <QPainter>
#include <QSaveFile>
#include <QStringList>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QtEndian>

namespace {

struct OutputFile {
    QString stagedPath;
    QString targetPath;
};

struct OriginalFile {
    bool existed = false;
    QByteArray bytes;
};

void appendLittleEndian16( QByteArray& bytes, quint16 value )
{
    const quint16 encoded = qToLittleEndian( value );
    bytes.append( reinterpret_cast<const char*>( &encoded ), sizeof( encoded ) );
}

void appendLittleEndian32( QByteArray& bytes, quint32 value )
{
    const quint32 encoded = qToLittleEndian( value );
    bytes.append( reinterpret_cast<const char*>( &encoded ), sizeof( encoded ) );
}

void appendBigEndian32( QByteArray& bytes, quint32 value )
{
    const quint32 encoded = qToBigEndian( value );
    bytes.append( reinterpret_cast<const char*>( &encoded ), sizeof( encoded ) );
}

bool writeFile( const QString& path, const QByteArray& bytes, QString& error )
{
    const QFileInfo info( path );
    if ( !QDir().mkpath( info.absolutePath() ) ) {
        error = QStringLiteral( "Cannot create directory %1" ).arg( info.absolutePath() );
        return false;
    }

    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        error = QStringLiteral( "Cannot write %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    if ( file.write( bytes ) != bytes.size() ) {
        error = QStringLiteral( "Incomplete write to %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    if ( !file.flush() ) {
        error = QStringLiteral( "Cannot flush %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    return true;
}

bool saveFileAtomically( const QString& path, const QByteArray& bytes, QString& error )
{
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        error = QStringLiteral( "Cannot stage target %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    if ( file.write( bytes ) != bytes.size() ) {
        file.cancelWriting();
        error = QStringLiteral( "Incomplete target write to %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    if ( !file.commit() ) {
        error = QStringLiteral( "Cannot commit target %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    return true;
}

QByteArray encodePng( const QImage& image, QString& error )
{
    QByteArray bytes;
    QBuffer buffer( &bytes );
    if ( !buffer.open( QIODevice::WriteOnly ) ) {
        error = QStringLiteral( "Cannot open PNG buffer" );
        return {};
    }

    QImageWriter writer( &buffer, "png" );
    if ( !writer.write( image ) ) {
        error = QStringLiteral( "Cannot encode PNG: %1" ).arg( writer.errorString() );
        return {};
    }
    return bytes;
}

QByteArray renderPng( QSvgRenderer& renderer, int size, QString& error )
{
    QImage image( size, size, QImage::Format_ARGB32_Premultiplied );
    image.fill( Qt::transparent );

    QPainter painter( &image );
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.setRenderHint( QPainter::SmoothPixmapTransform, true );
    renderer.render( &painter, QRectF( 0, 0, size, size ) );
    if ( !painter.end() ) {
        error = QStringLiteral( "Cannot finish rendering %1x%1 image" ).arg( size );
        return {};
    }
    return encodePng( image, error );
}

QByteArray encodeIco( const QList<int>& sizes, const QMap<int, QByteArray>& pngs )
{
    QByteArray ico;
    appendLittleEndian16( ico, 0 );
    appendLittleEndian16( ico, 1 );
    appendLittleEndian16( ico, static_cast<quint16>( sizes.size() ) );

    quint32 imageOffset = 6 + static_cast<quint32>( sizes.size() * 16 );
    for ( int size : sizes ) {
        ico.append( size == 256 ? '\0' : static_cast<char>( size ) );
        ico.append( size == 256 ? '\0' : static_cast<char>( size ) );
        ico.append( '\0' );
        ico.append( '\0' );
        appendLittleEndian16( ico, 1 );
        appendLittleEndian16( ico, 32 );
        appendLittleEndian32( ico, static_cast<quint32>( pngs.value( size ).size() ) );
        appendLittleEndian32( ico, imageOffset );
        imageOffset += static_cast<quint32>( pngs.value( size ).size() );
    }
    for ( int size : sizes ) {
        ico.append( pngs.value( size ) );
    }
    return ico;
}

QByteArray encodeIcns( const QList<QPair<QByteArray, int>>& chunks,
                       const QMap<int, QByteArray>& pngs )
{
    QByteArray body;
    for ( const auto& chunk : chunks ) {
        body.append( chunk.first );
        appendBigEndian32( body, static_cast<quint32>( 8 + pngs.value( chunk.second ).size() ) );
        body.append( pngs.value( chunk.second ) );
    }

    QByteArray icns( "icns" );
    appendBigEndian32( icns, static_cast<quint32>( 8 + body.size() ) );
    icns.append( body );
    return icns;
}

bool stageOutput( const QString& stagingRoot, const QString& name, const QString& target,
                  const QByteArray& bytes, QList<OutputFile>& outputs, QString& error )
{
    const QString stagedPath = QDir( stagingRoot ).filePath( name );
    if ( !writeFile( stagedPath, bytes, error ) ) {
        return false;
    }
    outputs.append( { stagedPath, QFileInfo( target ).absoluteFilePath() } );
    return true;
}

bool restoreOutputs( const QList<OutputFile>& outputs, const QList<OriginalFile>& originals,
                     int committedCount, QString& error )
{
    bool restored = true;
    QStringList failures;
    for ( int index = committedCount - 1; index >= 0; --index ) {
        if ( originals.at( index ).existed ) {
            QString restoreError;
            if ( !saveFileAtomically( outputs.at( index ).targetPath,
                                      originals.at( index ).bytes, restoreError ) ) {
                restored = false;
                failures.append( restoreError );
            }
        } else if ( !QFile::remove( outputs.at( index ).targetPath )
                    && QFileInfo::exists( outputs.at( index ).targetPath ) ) {
            restored = false;
            failures.append(
                QStringLiteral( "Cannot remove newly created %1" ).arg( outputs.at( index ).targetPath ) );
        }
    }
    if ( !restored ) {
        error = failures.join( QStringLiteral( "; " ) );
    }
    return restored;
}

bool commitOutputs( const QList<OutputFile>& outputs, QString& error )
{
    QList<OriginalFile> originals;
    originals.reserve( outputs.size() );

    for ( const OutputFile& output : outputs ) {
        const QFileInfo targetInfo( output.targetPath );
        if ( !QDir().mkpath( targetInfo.absolutePath() ) ) {
            error = QStringLiteral( "Cannot create target directory %1" ).arg( targetInfo.absolutePath() );
            return false;
        }

        OriginalFile original;
        original.existed = targetInfo.exists();
        if ( original.existed ) {
            QFile target( output.targetPath );
            if ( !target.open( QIODevice::ReadOnly ) ) {
                error = QStringLiteral( "Cannot back up %1: %2" )
                            .arg( output.targetPath, target.errorString() );
                return false;
            }
            original.bytes = target.readAll();
            if ( target.error() != QFileDevice::NoError ) {
                error = QStringLiteral( "Cannot read backup for %1: %2" )
                            .arg( output.targetPath, target.errorString() );
                return false;
            }
        }
        originals.append( original );
    }

    bool injectReadFailure = false;
    const int injectedReadFailureIndex = qEnvironmentVariableIntValue(
        "ZZLOGG_ICON_GENERATOR_TEST_FAIL_STAGED_READ_AT", &injectReadFailure );

    for ( int index = 0; index < outputs.size(); ++index ) {
        QFile staged( outputs.at( index ).stagedPath );
        if ( !staged.open( QIODevice::ReadOnly ) ) {
            error = QStringLiteral( "Cannot read staged output %1: %2" )
                        .arg( outputs.at( index ).stagedPath, staged.errorString() );
            QString rollbackError;
            restoreOutputs( outputs, originals, index, rollbackError );
            if ( !rollbackError.isEmpty() ) {
                error += QStringLiteral( "; rollback failed: %1" ).arg( rollbackError );
            }
            return false;
        }

        QByteArray stagedBytes = staged.readAll();
        const bool readFailureInjected = injectReadFailure && index == injectedReadFailureIndex;
        if ( readFailureInjected ) {
            stagedBytes.truncate( stagedBytes.size() / 2 );
        }
        if ( staged.error() != QFileDevice::NoError || readFailureInjected ) {
            error = readFailureInjected
                        ? QStringLiteral( "Injected staged read failure for %1" )
                              .arg( outputs.at( index ).stagedPath )
                        : QStringLiteral( "Cannot read staged output %1: %2" )
                              .arg( outputs.at( index ).stagedPath, staged.errorString() );
            QString rollbackError;
            restoreOutputs( outputs, originals, index, rollbackError );
            if ( !rollbackError.isEmpty() ) {
                error += QStringLiteral( "; rollback failed: %1" ).arg( rollbackError );
            }
            return false;
        }

        QString commitError;
        if ( !saveFileAtomically( outputs.at( index ).targetPath, stagedBytes, commitError ) ) {
            error = commitError;
            QString rollbackError;
            restoreOutputs( outputs, originals, index, rollbackError );
            if ( !rollbackError.isEmpty() ) {
                error += QStringLiteral( "; rollback failed: %1" ).arg( rollbackError );
            }
            return false;
        }
    }
    return true;
}

} // namespace

int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );
    QCoreApplication::setApplicationName( QStringLiteral( "zzlogg_icon_generator" ) );

    QCommandLineParser parser;
    parser.setApplicationDescription( QStringLiteral( "Generate deterministic ZzLogg icon assets" ) );
    parser.addHelpOption();
    const QCommandLineOption sourceOption( QStringList{ QStringLiteral( "source" ) },
                                           QStringLiteral( "SVG master path" ),
                                           QStringLiteral( "path" ) );
    const QCommandLineOption hicolorOption( QStringList{ QStringLiteral( "hicolor-root" ) },
                                            QStringLiteral( "hicolor image root" ),
                                            QStringLiteral( "path" ) );
    const QCommandLineOption icoOption( QStringList{ QStringLiteral( "ico" ) },
                                       QStringLiteral( "ICO output path" ),
                                       QStringLiteral( "path" ) );
    const QCommandLineOption icnsOption( QStringList{ QStringLiteral( "icns" ) },
                                        QStringLiteral( "ICNS output path" ),
                                        QStringLiteral( "path" ) );
    parser.addOptions( { sourceOption, hicolorOption, icoOption, icnsOption } );
    parser.process( app );

    if ( !parser.isSet( sourceOption ) || !parser.isSet( hicolorOption )
         || !parser.isSet( icoOption ) || !parser.isSet( icnsOption ) ) {
        parser.showHelp( 2 );
    }

    const QString sourcePath = QFileInfo( parser.value( sourceOption ) ).absoluteFilePath();
    QSvgRenderer renderer( sourcePath );
    if ( !renderer.isValid() ) {
        qCritical().noquote() << QStringLiteral( "Invalid SVG master: %1" ).arg( sourcePath );
        return 1;
    }

    const QList<int> hicolorSizes{ 16, 32, 48, 64, 128, 256, 512 };
    const QList<int> icoSizes{ 16, 20, 24, 32, 40, 48, 64, 128, 256 };
    const QList<QPair<QByteArray, int>> icnsChunks{ { QByteArrayLiteral( "icp4" ), 16 },
                                                    { QByteArrayLiteral( "icp5" ), 32 },
                                                    { QByteArrayLiteral( "icp6" ), 64 },
                                                    { QByteArrayLiteral( "ic07" ), 128 },
                                                    { QByteArrayLiteral( "ic08" ), 256 },
                                                    { QByteArrayLiteral( "ic09" ), 512 },
                                                    { QByteArrayLiteral( "ic10" ), 1024 } };
    const QList<int> renderSizes{ 16, 20, 24, 32, 40, 48, 64, 128, 256, 512, 1024 };

    QMap<int, QByteArray> pngs;
    QString error;
    for ( int size : renderSizes ) {
        const QByteArray png = renderPng( renderer, size, error );
        if ( png.isEmpty() ) {
            qCritical().noquote() << error;
            return 1;
        }
        const QImage verification = QImage::fromData( png, "PNG" );
        if ( verification.size() != QSize( size, size ) || !verification.hasAlphaChannel() ) {
            qCritical().noquote()
                << QStringLiteral( "Invalid staged PNG for %1x%1" ).arg( size );
            return 1;
        }
        pngs.insert( size, png );
    }

    const QByteArray ico = encodeIco( icoSizes, pngs );
    const QByteArray icns = encodeIcns( icnsChunks, pngs );
    QTemporaryDir stagingDir( QDir::tempPath() + QStringLiteral( "/zzlogg-icons-XXXXXX" ) );
    if ( !stagingDir.isValid() ) {
        qCritical().noquote() << QStringLiteral( "Cannot create icon staging directory" );
        return 1;
    }

    QList<OutputFile> outputs;
    const QString hicolorRoot = QFileInfo( parser.value( hicolorOption ) ).absoluteFilePath();
    for ( int size : hicolorSizes ) {
        const QString relative =
            QStringLiteral( "%1x%1/ZzLogg.png" ).arg( size );
        if ( !stageOutput( stagingDir.path(), relative, QDir( hicolorRoot ).filePath( relative ),
                           pngs.value( size ), outputs, error ) ) {
            qCritical().noquote() << error;
            return 1;
        }
    }
    if ( !stageOutput( stagingDir.path(), QStringLiteral( "container/ZzLogg.ico" ),
                       parser.value( icoOption ), ico, outputs, error )
         || !stageOutput( stagingDir.path(), QStringLiteral( "container/ZzLogg.icns" ),
                          parser.value( icnsOption ), icns, outputs, error ) ) {
        qCritical().noquote() << error;
        return 1;
    }

    if ( !commitOutputs( outputs, error ) ) {
        qCritical().noquote() << error;
        return 1;
    }

    qInfo().noquote() << QStringLiteral( "Generated %1 deterministic icon assets from %2" )
                             .arg( outputs.size() )
                             .arg( sourcePath );
    return 0;
}
