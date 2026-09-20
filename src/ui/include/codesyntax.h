// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QVector>
#include <functional>
#include <memory>
#include <optional>

// Document-scoped syntax state. All access is on the GUI thread; parsing yields
// between small batches. Views query ORIGINAL file lines, even when filtered.
class CodeSyntax : public QObject {
    Q_OBJECT
public:
    struct Span {
        int offset;
        int length;
        QColor color;
    };
    using ReadLine = std::function<std::optional<QString>( quint64 )>;
    using LineCount = std::function<quint64()>;
    CodeSyntax( ReadLine readLine, LineCount lineCount, QObject* parent = nullptr );
    ~CodeSyntax() override;
    void setFileName( const QString& fileName );
    void setLanguage( const QString& language );
    QString language() const;
    QString definitionName() const;
    void invalidate();
    QVector<Span> formats( quint64 line, bool dark );
    quint64 plainTextFrom() const;
Q_SIGNALS:
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
    void chooseDefinition();
    void processBatch();
};
