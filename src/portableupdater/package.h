#pragma once
#include "transaction.h"
#include <QByteArray>
#include <QString>
#include <atomic>
namespace zzlogg::portable {
struct PreparedPackage { QByteArray oldManifestSha256; std::vector<std::string> oldFiles; std::vector<File> files; };
PreparedPackage preparePackage(const QString& zipPath, const QByteArray& expectedSha256,
    qint64 expectedSize, const QString& installRoot, const QString& stage,
    const std::atomic_bool& cancelled);
}
