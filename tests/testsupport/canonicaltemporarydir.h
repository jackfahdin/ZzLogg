#pragma once

#include <QDir>
#include <QTemporaryDir>

// macOS exposes its temporary directory through /var -> /private/var. Tests
// of symlink rejection need a real root so only links they create are rejected.
// Canonicalize the existing temporary parent before creating any test payload;
// never canonicalize payload paths, which may deliberately contain unsafe links.
class CanonicalTemporaryDir final : public QTemporaryDir {
  public:
    CanonicalTemporaryDir()
        : QTemporaryDir( QDir{ temporaryRoot() }.filePath( QStringLiteral( "zzlogg-test-XXXXXX" ) ) )
    {
    }

  private:
    static QString temporaryRoot()
    {
        const QDir temporary{ QDir::tempPath() };
        const QString canonical = temporary.canonicalPath();
        return canonical.isEmpty() ? temporary.absolutePath() : canonical;
    }
};
