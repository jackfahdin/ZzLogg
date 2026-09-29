// SPDX-License-Identifier: GPL-3.0-or-later
#include "codesyntax.h"

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QCache>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QTimer>
#include <algorithm>
#include <iterator>
#include <map>
#include <set>

namespace {
constexpr quint64 MaxLines = 100000;
constexpr int MaxLineLength = 16384;
constexpr quint64 CheckpointInterval = 1024;
// key is the stable session/menu token; definition is the KSyntaxHighlighting
// repository name. Menu order follows this table.
struct LanguageSpec {
    const char* key;
    const char* definition;
};
constexpr LanguageSpec kLanguages[] = {
    { "bash", "Bash" },         { "cpp", "C++" },      { "csharp", "C#" },
    { "cmake", "CMake" },       { "css", "CSS" },      { "diff", "Diff" },
    { "go", "Go" },             { "html", "HTML" },    { "ini", "INI Files" },
    { "java", "Java" },         { "js", "JavaScript" }, { "json", "JSON" },
    { "log", "Log File (advanced)" }, { "lua", "Lua" }, { "make", "Makefile" },
    { "markdown", "Markdown" }, { "powershell", "PowerShell" }, { "python", "Python" },
    { "rust", "Rust" },         { "sql", "SQL" },      { "toml", "TOML" },
    { "ts", "TypeScript" },     { "xml", "XML" },      { "yaml", "YAML" },
};
const char* definitionForKey( const QString& key )
{
    for ( const auto& spec : kLanguages ) {
        if ( key == QLatin1String( spec.key ) )
            return spec.definition;
    }
    return nullptr;
}
QString keyForSuffix( const QString& fileName )
{
    const QFileInfo info( fileName );
    const auto name = info.fileName();
    if ( name.compare( QLatin1String( "CMakeLists.txt" ), Qt::CaseInsensitive ) == 0 )
        return QStringLiteral( "cmake" );
    if ( name.compare( QLatin1String( "Makefile" ), Qt::CaseInsensitive ) == 0 )
        return QStringLiteral( "make" );
    const auto suffix = info.suffix().toLower();
    static const QHash<QString, QString> map{
        { "c", "cpp" },         { "cc", "cpp" },   { "cpp", "cpp" },  { "cxx", "cpp" },
        { "h", "cpp" },         { "hh", "cpp" },   { "hpp", "cpp" },  { "hxx", "cpp" },
        { "ipp", "cpp" },       { "inl", "cpp" },  { "cs", "csharp" }, { "java", "java" },
        { "js", "js" },         { "mjs", "js" },   { "cjs", "js" },   { "ts", "ts" },
        { "json", "json" },     { "py", "python" }, { "pyw", "python" }, { "rs", "rust" },
        { "go", "go" },         { "xml", "xml" },  { "yaml", "yaml" }, { "yml", "yaml" },
        { "md", "markdown" },   { "markdown", "markdown" }, { "cmake", "cmake" },
        { "sh", "bash" },       { "bash", "bash" }, { "ps1", "powershell" },
        { "psm1", "powershell" }, { "ini", "ini" }, { "cfg", "ini" }, { "diff", "diff" },
        { "patch", "diff" },    { "sql", "sql" },  { "html", "html" }, { "htm", "html" },
        { "css", "css" },       { "toml", "toml" }, { "lua", "lua" },  { "mk", "make" },
        { "mak", "make" },      { "log", "log" },
    };
    return map.value( suffix );
}
struct StoredSpan {
    int offset;
    int length;
    QColor light;
    QColor dark;
};
using StoredSpans = QVector<StoredSpan>;
class Lexer : public KSyntaxHighlighting::AbstractHighlighter {
public:
    StoredSpans spans;
    bool overflow = false;
    KSyntaxHighlighting::Theme light, dark;
    using AbstractHighlighter::highlightLine;

protected:
    void applyFormat( int offset, int length, const KSyntaxHighlighting::Format& format ) override
    {
        if ( overflow || length <= 0
             || ( !format.hasTextColor( light ) && !format.hasTextColor( dark ) ) )
            return;
        const auto lightColor = format.textColor( light );
        const auto darkColor = format.textColor( dark );
        if ( !spans.isEmpty() && spans.back().offset + spans.back().length == offset
             && spans.back().light == lightColor && spans.back().dark == darkColor ) {
            spans.back().length += length;
        }
        else if ( spans.size() < 128 ) {
            spans.push_back( { offset, length, lightColor, darkColor } );
        }
        else {
            // Dense minified text stays plain, but lexing continues so the next
            // line still receives the correct multiline state.
            spans.clear();
            overflow = true;
        }
    }
};
} // namespace

class CodeSyntax::Private {
public:
    ReadLine readLine;
    LineCount lineCount;
    KSyntaxHighlighting::Repository repository;
    Lexer lexer;
    QString fileName;
    QString language = QStringLiteral( "auto" );
    QTimer timer;
    // Cost includes a minimum per line, bounding both entries and format bytes.
    QCache<quint64, StoredSpans> cache{ 4 * 1024 * 1024 };
    std::map<quint64, KSyntaxHighlighting::State> checkpoints;
    std::set<quint64> pending;
    KSyntaxHighlighting::State state;
    quint64 nextLine = 0;
    quint64 plainFrom = MaxLines;
};

CodeSyntax::CodeSyntax( ReadLine readLine, LineCount lineCount, QObject* parent )
    : QObject( parent )
    , d( std::make_unique<Private>() )
{
    d->readLine = std::move( readLine );
    d->lineCount = std::move( lineCount );
    d->lexer.light = d->repository.defaultTheme( KSyntaxHighlighting::Repository::LightTheme );
    d->lexer.dark = d->repository.defaultTheme( KSyntaxHighlighting::Repository::DarkTheme );
    d->lexer.setTheme( d->lexer.light );
    d->timer.setSingleShot( true );
    connect( &d->timer, &QTimer::timeout, this, &CodeSyntax::processBatch );
    invalidate();
}
CodeSyntax::~CodeSyntax() = default;

QString CodeSyntax::language() const
{
    return d->language;
}
QString CodeSyntax::definitionName() const
{
    const auto definition = d->lexer.definition();
    return definition.isValid() ? definition.name() : QString{};
}
quint64 CodeSyntax::plainTextFrom() const
{
    return d->plainFrom;
}

void CodeSyntax::setFileName( const QString& fileName )
{
    if ( d->fileName == fileName )
        return;
    d->fileName = fileName;
    if ( d->language == QLatin1String( "auto" ) )
        chooseDefinition();
}
QVector<CodeSyntax::LanguageEntry> CodeSyntax::availableLanguages()
{
    QVector<LanguageEntry> entries;
    entries.reserve( std::size( kLanguages ) );
    for ( const auto& spec : kLanguages ) {
        entries.push_back( { QString::fromLatin1( spec.key ),
                             QString::fromLatin1( spec.definition ) } );
    }
    return entries;
}
void CodeSyntax::setLanguage( const QString& language )
{
    const bool known = language == QLatin1String( "auto" )
                       || language == QLatin1String( "plain" )
                       || definitionForKey( language ) != nullptr;
    const QString normalized = known ? language : QStringLiteral( "auto" );
    if ( d->language == normalized )
        return;
    d->language = normalized;
    chooseDefinition();
}
void CodeSyntax::chooseDefinition()
{
    auto language = d->language;
    if ( language == QLatin1String( "auto" ) ) {
        language = keyForSuffix( d->fileName );
    }
    if ( language == QLatin1String( "plain" ) )
        language.clear();
    QString name;
    if ( const auto* definition = definitionForKey( language ) )
        name = QString::fromLatin1( definition );
    d->lexer.setDefinition( d->repository.definitionForName( name ) );
    invalidate();
}
void CodeSyntax::invalidate()
{
    d->timer.stop();
    d->pending.clear();
    d->cache.clear();
    d->checkpoints.clear();
    d->state = {};
    d->checkpoints.emplace( 0, d->state );
    d->nextLine = 0;
    d->plainFrom = MaxLines;
    Q_EMIT changed();
}
QVector<CodeSyntax::Span> CodeSyntax::formats( quint64 line, bool dark )
{
    QVector<Span> result;
    if ( !d->lexer.definition().isValid() || line >= d->lineCount() || line >= d->plainFrom )
        return result;
    if ( const auto* cached = d->cache.object( line ) ) {
        result.reserve( cached->size() );
        for ( const auto& span : *cached ) {
            const auto color = dark ? span.dark : span.light;
            if ( color.isValid() )
                result.push_back( { span.offset, span.length, color } );
        }
    }
    else if ( d->pending.size() < 4096 ) {
        d->pending.insert( line );
        if ( !d->timer.isActive() )
            d->timer.start( 1 );
    }
    return result;
}
void CodeSyntax::processBatch()
{
    QElapsedTimer budget;
    budget.start();
    bool visibleChanged = false;
    for ( int batch = 0; batch < 32 && budget.elapsed() < 3 && !d->pending.empty(); ++batch ) {
        const quint64 target = *d->pending.begin();
        if ( target >= d->lineCount() || target >= d->plainFrom ) {
            d->pending.erase( d->pending.begin() );
            continue;
        }
        // Resume at the closest known state, never at a guessed empty state.
        auto checkpoint = std::prev( d->checkpoints.upper_bound( target ) );
        if ( d->nextLine > target || d->nextLine < checkpoint->first ) {
            d->nextLine = checkpoint->first;
            d->state = checkpoint->second;
        }
        const auto text = d->readLine( d->nextLine );
        if ( !text || text->size() > MaxLineLength ) {
            // Skipping a line could lose a comment/raw string opener. Keep all
            // subsequent lines plain instead of propagating an invented state.
            d->plainFrom = d->nextLine;
            d->pending.clear();
            visibleChanged = true;
            break;
        }
        d->lexer.spans.clear();
        d->lexer.overflow = false;
        d->state = d->lexer.highlightLine( *text, d->state );
        if ( d->pending.erase( d->nextLine ) != 0 ) {
            const int cost = std::max( 1024, static_cast<int>( d->lexer.spans.size() )
                                                 * static_cast<int>( sizeof( StoredSpan ) ) );
            d->cache.insert( d->nextLine, new StoredSpans( d->lexer.spans ), cost );
            visibleChanged = true;
        }
        ++d->nextLine;
        if ( d->nextLine % CheckpointInterval == 0 )
            d->checkpoints.insert_or_assign( d->nextLine, d->state );
    }
    if ( !d->pending.empty() )
        d->timer.start( 1 );
    if ( visibleChanged )
        Q_EMIT changed();
}
