#include "zzlogg_brand.h"

#include <QFile>
#include <QIcon>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QSize>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <QtEndian>
#include <QtTest>

namespace {

QByteArray readFile( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return {};
    }
    return file.readAll();
}

quint16 littleEndian16( const QByteArray& bytes, qsizetype offset )
{
    return qFromLittleEndian<quint16>( reinterpret_cast<const uchar*>( bytes.constData() + offset ) );
}

quint32 littleEndian32( const QByteArray& bytes, qsizetype offset )
{
    return qFromLittleEndian<quint32>( reinterpret_cast<const uchar*>( bytes.constData() + offset ) );
}

quint32 bigEndian32( const QByteArray& bytes, qsizetype offset )
{
    return qFromBigEndian<quint32>( reinterpret_cast<const uchar*>( bytes.constData() + offset ) );
}

} // namespace

class IconAssetTest final : public QObject {
    Q_OBJECT

  private slots:
    void svgMasterHasApprovedStructure();
    void pngAssetsHaveExactDimensions();
    void icoContainsRequiredPngFrames();
    void icnsContainsRequiredPngChunks();
    void qrcIconLoadsThroughBrandResource();
};

void IconAssetTest::svgMasterHasApprovedStructure()
{
    const QString path = QStringLiteral( ZZLOGG_HICOLOR_ROOT "/scalable/ZzLogg.svg" );
    const QByteArray svg = readFile( path );
    QVERIFY2( !svg.isEmpty(), qPrintable( QStringLiteral( "Cannot read %1" ).arg( path ) ) );
    QVERIFY( QSvgRenderer( svg ).isValid() );

    QXmlStreamReader xml( svg );
    QSet<QString> ids;
    QStringList ancestorIds;
    bool hasTextElement = false;
    bool logLinesInStack = false;
    bool handleInGlass = false;
    bool zInGlass = false;
    QString logLinesOpacity;
    QString zPath;
    QString zFill;
    QString zStroke;
    QString zStrokeWidth;
    QString zStrokeLinejoin;
    QList<QMap<QString, QString>> logLines;
    QList<QMap<QString, QString>> handlePaths;
    int stackPosition = -1;
    int glassPosition = -1;
    int handlePosition = -1;
    int lensPosition = -1;
    int zPosition = -1;
    int highlightPosition = -1;
    int elementPosition = 0;

    while ( !xml.atEnd() ) {
        xml.readNext();
        if ( xml.isEndElement() ) {
            ancestorIds.removeLast();
            continue;
        }
        if ( !xml.isStartElement() ) {
            continue;
        }

        ++elementPosition;
        const auto attributes = xml.attributes();
        const QString id = attributes.value( QStringLiteral( "id" ) ).toString();
        if ( !id.isEmpty() ) {
            ids.insert( id );
        }
        hasTextElement = hasTextElement || xml.name() == QStringLiteral( "text" );

        if ( id == QStringLiteral( "layer1" ) ) {
            stackPosition = elementPosition;
        } else if ( id == QStringLiteral( "layer3" ) ) {
            glassPosition = elementPosition;
        }

        if ( id == QStringLiteral( "zzlogg-handle" ) ) {
            handlePosition = elementPosition;
            handleInGlass = ancestorIds.contains( QStringLiteral( "layer3" ) );
        } else if ( id == QStringLiteral( "zzlogg-log-lines" ) ) {
            logLinesInStack = ancestorIds.contains( QStringLiteral( "layer1" ) );
            logLinesOpacity = attributes.value( QStringLiteral( "opacity" ) ).toString();
        } else if ( id == QStringLiteral( "path4452" ) ) {
            lensPosition = elementPosition;
        } else if ( id == QStringLiteral( "zzlogg-z" ) ) {
            zPosition = elementPosition;
            zPath = attributes.value( QStringLiteral( "d" ) ).toString();
            zFill = attributes.value( QStringLiteral( "fill" ) ).toString();
            zStroke = attributes.value( QStringLiteral( "stroke" ) ).toString();
            zStrokeWidth = attributes.value( QStringLiteral( "stroke-width" ) ).toString();
            zStrokeLinejoin = attributes.value( QStringLiteral( "stroke-linejoin" ) ).toString();
            zInGlass = ancestorIds.contains( QStringLiteral( "layer3" ) );
        } else if ( id == QStringLiteral( "path4462" ) ) {
            highlightPosition = elementPosition;
        }

        if ( ancestorIds.contains( QStringLiteral( "zzlogg-log-lines" ) )
             && xml.name() == QStringLiteral( "rect" ) ) {
            QMap<QString, QString> values;
            for ( const QString& name : { QStringLiteral( "x" ), QStringLiteral( "y" ),
                                          QStringLiteral( "width" ), QStringLiteral( "height" ),
                                          QStringLiteral( "rx" ), QStringLiteral( "fill" ) } ) {
                values.insert( name, attributes.value( name ).toString() );
            }
            logLines.append( values );
        }
        if ( ancestorIds.contains( QStringLiteral( "zzlogg-handle" ) )
             && xml.name() == QStringLiteral( "path" ) ) {
            QMap<QString, QString> values;
            for ( const QString& name : { QStringLiteral( "d" ), QStringLiteral( "fill" ),
                                          QStringLiteral( "stroke" ),
                                          QStringLiteral( "stroke-width" ),
                                          QStringLiteral( "opacity" ) } ) {
                values.insert( name, attributes.value( name ).toString() );
            }
            handlePaths.append( values );
        }
        ancestorIds.append( id );
    }

    QVERIFY2( !xml.hasError(), qPrintable( xml.errorString() ) );
    QVERIFY( ids.contains( QStringLiteral( "zzlogg-log-lines" ) ) );
    QVERIFY( ids.contains( QStringLiteral( "zzlogg-handle" ) ) );
    QVERIFY( ids.contains( QStringLiteral( "zzlogg-z" ) ) );
    QVERIFY( !ids.contains( QStringLiteral( "zzlogg-log-field" ) ) );
    QVERIFY( !hasTextElement );
    QVERIFY( logLinesInStack );
    QVERIFY( handleInGlass );
    QVERIFY( zInGlass );
    QCOMPARE( logLinesOpacity, QStringLiteral( "0.92" ) );
    QCOMPARE( zPath,
              QStringLiteral( "M 12.2,10.8 H 24.6 V 13.7 L 17.1,21.7 H 24.8 V 24.8 H 11.8 V 22.0 L 19.4,13.9 H 12.2 Z" ) );
    QCOMPARE( zFill, QStringLiteral( "#275fa8" ) );
    QCOMPARE( zStroke, QStringLiteral( "#eef6ff" ) );
    QCOMPARE( zStrokeWidth, QStringLiteral( "0.5" ) );
    QCOMPARE( zStrokeLinejoin, QStringLiteral( "round" ) );

    const QList<QMap<QString, QString>> expectedLogLines{
        { { QStringLiteral( "x" ), QStringLiteral( "9.5625" ) },
          { QStringLiteral( "y" ), QStringLiteral( "7.125" ) },
          { QStringLiteral( "width" ), QStringLiteral( "17.53125" ) },
          { QStringLiteral( "height" ), QStringLiteral( "1.78125" ) },
          { QStringLiteral( "rx" ), QStringLiteral( "0.28125" ) },
          { QStringLiteral( "fill" ), QStringLiteral( "#3b82f6" ) } },
        { { QStringLiteral( "x" ), QStringLiteral( "7.6875" ) },
          { QStringLiteral( "y" ), QStringLiteral( "10.96875" ) },
          { QStringLiteral( "width" ), QStringLiteral( "7.03125" ) },
          { QStringLiteral( "height" ), QStringLiteral( "1.6875" ) },
          { QStringLiteral( "rx" ), QStringLiteral( "0.28125" ) },
          { QStringLiteral( "fill" ), QStringLiteral( "#ef4444" ) } },
        { { QStringLiteral( "x" ), QStringLiteral( "9.5625" ) },
          { QStringLiteral( "y" ), QStringLiteral( "14.8125" ) },
          { QStringLiteral( "width" ), QStringLiteral( "5.34375" ) },
          { QStringLiteral( "height" ), QStringLiteral( "1.6875" ) },
          { QStringLiteral( "rx" ), QStringLiteral( "0.28125" ) },
          { QStringLiteral( "fill" ), QStringLiteral( "#f59e0b" ) } },
        { { QStringLiteral( "x" ), QStringLiteral( "9.5625" ) },
          { QStringLiteral( "y" ), QStringLiteral( "18.75" ) },
          { QStringLiteral( "width" ), QStringLiteral( "5.34375" ) },
          { QStringLiteral( "height" ), QStringLiteral( "1.6875" ) },
          { QStringLiteral( "rx" ), QStringLiteral( "0.28125" ) },
          { QStringLiteral( "fill" ), QStringLiteral( "#22c55e" ) } },
        { { QStringLiteral( "x" ), QStringLiteral( "9.5625" ) },
          { QStringLiteral( "y" ), QStringLiteral( "22.59375" ) },
          { QStringLiteral( "width" ), QStringLiteral( "4.96875" ) },
          { QStringLiteral( "height" ), QStringLiteral( "1.6875" ) },
          { QStringLiteral( "rx" ), QStringLiteral( "0.28125" ) },
          { QStringLiteral( "fill" ), QStringLiteral( "#8b5cf6" ) } }
    };
    QCOMPARE( logLines, expectedLogLines );

    const QList<QMap<QString, QString>> expectedHandlePaths{
        { { QStringLiteral( "d" ), QStringLiteral( "M 29.1,30.2 43.5,41.6" ) },
          { QStringLiteral( "fill" ), QStringLiteral( "none" ) },
          { QStringLiteral( "stroke" ), QStringLiteral( "#9a5b16" ) },
          { QStringLiteral( "stroke-width" ), QStringLiteral( "5.2" ) },
          { QStringLiteral( "opacity" ), QStringLiteral( "0.78" ) } },
        { { QStringLiteral( "d" ), QStringLiteral( "M 29.1,30.2 43.5,41.6" ) },
          { QStringLiteral( "fill" ), QStringLiteral( "none" ) },
          { QStringLiteral( "stroke" ), QStringLiteral( "#f2a735" ) },
          { QStringLiteral( "stroke-width" ), QStringLiteral( "3.7" ) },
          { QStringLiteral( "opacity" ), QStringLiteral( "0.82" ) } },
        { { QStringLiteral( "d" ), QStringLiteral( "M 29.7,29.7 42.6,40.1" ) },
          { QStringLiteral( "fill" ), QStringLiteral( "none" ) },
          { QStringLiteral( "stroke" ), QStringLiteral( "#ffe0a3" ) },
          { QStringLiteral( "stroke-width" ), QStringLiteral( "0.95" ) },
          { QStringLiteral( "opacity" ), QStringLiteral( "0.62" ) } }
    };
    QCOMPARE( handlePaths, expectedHandlePaths );
    QVERIFY( stackPosition > 0 );
    QVERIFY( stackPosition < glassPosition );
    QVERIFY( handlePosition > 0 );
    QVERIFY( handlePosition < lensPosition );
    QVERIFY( zPosition > lensPosition );
    QVERIFY( zPosition < highlightPosition );
}

void IconAssetTest::pngAssetsHaveExactDimensions()
{
    const QList<int> pngSizes{ 16, 32, 48, 64, 128, 256, 512 };
    for ( int size : pngSizes ) {
        const QString path =
            QStringLiteral( ZZLOGG_HICOLOR_ROOT "/%1x%1/ZzLogg.png" ).arg( size );
        QImage image( path );
        QVERIFY2( !image.isNull(), qPrintable( QStringLiteral( "Cannot load %1" ).arg( path ) ) );
        QCOMPARE( image.size(), QSize( size, size ) );
        QVERIFY( image.hasAlphaChannel() );
    }
}

void IconAssetTest::icoContainsRequiredPngFrames()
{
    const QByteArray ico = readFile( QStringLiteral( ZZLOGG_ICO_PATH ) );
    QVERIFY( ico.size() >= 6 );
    QCOMPARE( littleEndian16( ico, 0 ), quint16( 0 ) );
    QCOMPARE( littleEndian16( ico, 2 ), quint16( 1 ) );

    const quint16 count = littleEndian16( ico, 4 );
    QCOMPARE( count, quint16( 9 ) );
    QVERIFY( ico.size() >= 6 + count * 16 );

    QSet<int> sizes;
    for ( quint16 index = 0; index < count; ++index ) {
        const qsizetype entry = 6 + index * 16;
        const int width = static_cast<uchar>( ico.at( entry ) ) == 0
                              ? 256
                              : static_cast<uchar>( ico.at( entry ) );
        const int height = static_cast<uchar>( ico.at( entry + 1 ) ) == 0
                               ? 256
                               : static_cast<uchar>( ico.at( entry + 1 ) );
        QCOMPARE( height, width );
        const quint32 byteCount = littleEndian32( ico, entry + 8 );
        const quint32 imageOffset = littleEndian32( ico, entry + 12 );
        QVERIFY( imageOffset <= static_cast<quint32>( ico.size() ) );
        QVERIFY( byteCount <= static_cast<quint32>( ico.size() ) - imageOffset );
        const QByteArray payload = ico.mid( imageOffset, byteCount );
        QVERIFY( payload.startsWith( QByteArray::fromHex( "89504e470d0a1a0a" ) ) );
        const QImage frame = QImage::fromData( payload, "PNG" );
        QCOMPARE( frame.size(), QSize( width, height ) );
        sizes.insert( width );
    }

    QCOMPARE( sizes, QSet<int>( { 16, 20, 24, 32, 40, 48, 64, 128, 256 } ) );
}

void IconAssetTest::icnsContainsRequiredPngChunks()
{
    const QByteArray icns = readFile( QStringLiteral( ZZLOGG_ICNS_PATH ) );
    QVERIFY( icns.size() >= 8 );
    QCOMPARE( icns.left( 4 ), QByteArrayLiteral( "icns" ) );
    QCOMPARE( bigEndian32( icns, 4 ), quint32( icns.size() ) );

    const QMap<QByteArray, int> requiredChunks{ { QByteArrayLiteral( "icp4" ), 16 },
                                                { QByteArrayLiteral( "icp5" ), 32 },
                                                { QByteArrayLiteral( "icp6" ), 64 },
                                                { QByteArrayLiteral( "ic07" ), 128 },
                                                { QByteArrayLiteral( "ic08" ), 256 },
                                                { QByteArrayLiteral( "ic09" ), 512 },
                                                { QByteArrayLiteral( "ic10" ), 1024 } };
    QSet<QByteArray> chunks;
    qsizetype offset = 8;
    while ( offset < icns.size() ) {
        QVERIFY( offset + 8 <= icns.size() );
        const QByteArray type = icns.mid( offset, 4 );
        const quint32 chunkSize = bigEndian32( icns, offset + 4 );
        QVERIFY( chunkSize >= 8 );
        QVERIFY( chunkSize <= static_cast<quint32>( icns.size() - offset ) );
        const QByteArray payload = icns.mid( offset + 8, chunkSize - 8 );
        if ( requiredChunks.contains( type ) ) {
            QVERIFY( payload.startsWith( QByteArray::fromHex( "89504e470d0a1a0a" ) ) );
            const QImage frame = QImage::fromData( payload, "PNG" );
            QCOMPARE( frame.size(), QSize( requiredChunks.value( type ), requiredChunks.value( type ) ) );
        }
        chunks.insert( type );
        offset += chunkSize;
    }
    QCOMPARE( offset, icns.size() );
    for ( auto it = requiredChunks.cbegin(); it != requiredChunks.cend(); ++it ) {
        QVERIFY2( chunks.contains( it.key() ), it.key().constData() );
    }
}

void IconAssetTest::qrcIconLoadsThroughBrandResource()
{
    const QIcon icon( QString::fromLatin1( zzlogg::brand::IconResource ) );
    QVERIFY( !icon.isNull() );
    const QPixmap pixmap = icon.pixmap( QSize( 48, 48 ) );
    QCOMPARE( pixmap.size(), QSize( 48, 48 ) );
    QVERIFY( pixmap.hasAlphaChannel() );
}

QTEST_MAIN( IconAssetTest )

#include "iconassettest.moc"
