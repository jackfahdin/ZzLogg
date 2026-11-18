#include "corpus.h"
#include "harness.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>

namespace {
void keys(const QJsonObject& object, const QSet<QString>& allowed)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        workerprobe::require(allowed.contains(it.key()), "Unknown corpus field");
}
QString string(const QJsonObject& object, const QString& key, bool optional = false)
{
    if (optional && !object.contains(key)) return {};
    workerprobe::require(object.value(key).isString(), "Corpus string field missing or invalid");
    return object.value(key).toString();
}
bool boolean(const QJsonObject& object, const QString& key, bool fallback)
{
    if (!object.contains(key)) return fallback;
    workerprobe::require(object.value(key).isBool(), "Corpus boolean field has wrong type");
    return object.value(key).toBool();
}
int integer(const QJsonObject& object, const QString& key, int fallback, int maximum)
{
    if (!object.contains(key)) return fallback;
    const auto value = object.value(key);
    const auto number = value.toDouble();
    workerprobe::require(value.isDouble() && std::isfinite(number) && number >= 1
                         && number <= maximum && std::floor(number) == number,
                         "Corpus integer field out of range");
    return static_cast<int>(number);
}
}
std::vector<Scenario> loadCorpus(const QString& manifest)
{
    workerprobe::require(QFileInfo(manifest).isFile(), "Manifest must be a regular file");
    QFile file(manifest);
    workerprobe::require(file.open(QIODevice::ReadOnly), "Cannot open corpus manifest");
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &error);
    workerprobe::require(error.error == QJsonParseError::NoError && json.isObject(), "Invalid corpus JSON");
    const auto object = json.object();
    keys(object, {"version", "cases"});
    workerprobe::require(object.value("version").isDouble() && object.value("version").toDouble() == 1
                         && object.value("cases").isArray() && !object.value("cases").toArray().isEmpty(),
                         "Expected corpus version 1 and nonempty cases");
    std::vector<Scenario> result;
    QSet<QString> ids;
    for (const auto& entry : object.value("cases").toArray()) {
        workerprobe::require(entry.isObject(), "Corpus case is not an object");
        const auto value = entry.toObject();
        keys(value, {"id", "file", "encoding", "pattern", "kind", "case_sensitive", "inverse",
                     "threads", "chunk_lines", "prefilter", "timeout_ms"});
        Scenario scenario;
        const auto id = string(value, "id");
        workerprobe::require(QRegularExpression("\\A[A-Za-z0-9][A-Za-z0-9._-]{0,63}\\z").match(id).hasMatch()
                             && !ids.contains(id), "Invalid or duplicate corpus id");
        ids.insert(id);
        scenario.name = id.toStdString();
        const auto path = string(value, "file");
        workerprobe::require(!path.isEmpty(), "Empty corpus file path");
        scenario.path = QFileInfo(manifest).absoluteDir().absoluteFilePath(path);
        workerprobe::require(QFileInfo(scenario.path).isFile(), "Corpus input must be a regular file");
        scenario.codec = string(value, "encoding").toLatin1();
        workerprobe::require(QTextCodec::codecForName(scenario.codec) != nullptr, "Unknown corpus encoding");
        const auto kind = string(value, "kind");
        workerprobe::require(kind == "literal" || kind == "regex" || kind == "boolean-literal"
                             || kind == "boolean-regex", "Unknown corpus query kind");
        scenario.query = {string(value, "pattern"), boolean(value, "case_sensitive", true),
                          boolean(value, "inverse", false), kind.startsWith("boolean-"), kind.endsWith("literal")};
        workerprobe::require(RegularExpression(scenario.query).isValid(), "Invalid corpus query");
        scenario.prefilter = string(value, "prefilter", true);
        workerprobe::require(QRegularExpression(scenario.prefilter).isValid(), "Invalid corpus prefilter");
        scenario.threads = static_cast<unsigned>(integer(value, "threads", 1, 256));
        scenario.chunk = static_cast<unsigned>(integer(value, "chunk_lines", 1024, 1000000));
        scenario.timeoutMs = integer(value, "timeout_ms", 30000, 3600000);
        result.push_back(std::move(scenario));
    }
    return result;
}
QString inputHash(const Scenario& s, const QByteArray& fileHash)
{
    // Structured fields avoid concatenation ambiguities and include every
    // query/routing option, even when two settings happen to yield equal rows.
    const QJsonObject identity{{"file_sha256", QString::fromLatin1(fileHash)},
        {"pattern", s.query.pattern}, {"case_sensitive", s.query.isCaseSensitive},
        {"inverse", s.query.isExclude}, {"boolean", s.query.isBoolean},
        {"plain_text", s.query.isPlainText}, {"encoding", QString::fromLatin1(s.codec)},
        {"prefilter", s.prefilter}, {"threads", int(s.threads)}, {"chunk_lines", int(s.chunk)},
        {"timeout_ms", s.timeoutMs}, {"signature_version", 1}};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                                      QCryptographicHash::Sha256).toHex());
}
