#pragma once
#include "regularexpressionpattern.h"
#include <QByteArray>
#include <vector>

struct Scenario {
    std::string name;
    QByteArray bytes;
    QString path; // Empty for generated input.
    RegularExpressionPattern query;
    QByteArray codec = "UTF-8";
    QString prefilter;
    unsigned threads = 1;
    unsigned chunk = 1024;
    int timeoutMs = 30000;
};
std::vector<Scenario> loadCorpus(const QString& manifest);
QString inputHash(const Scenario&, const QByteArray& fileHash);
