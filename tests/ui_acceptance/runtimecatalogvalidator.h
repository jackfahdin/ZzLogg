#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

struct RuntimeCatalogValidationResult {
    bool valid = false;
    QString error;
    QHash<QString, QString> translations;
};

RuntimeCatalogValidationResult validateRuntimeCatalog( const QByteArray& xml,
                                                       const QStringList& requiredSources );
