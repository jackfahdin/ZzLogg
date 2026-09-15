#include "fixturehelper.h"
#include <QCoreApplication>
#include <QFile>
#include <charconv>
#include <cstdio>
#include <limits>

namespace {
int usage() {
    std::fputs("Test fixtures only. Usage:\n"
        "  zzlogg_update_testfeed generate --output <new-file>\n"
        "  zzlogg_update_testfeed verify --input <file> --now <unix-seconds>\n",stderr);
    return 2;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()==4 && args[1]=="generate" && args[2]=="--output") {
        const auto wire=update_fixture::envelope(update_fixture::payload().dump());
        QFile output(args[3]);
        // NewOnly is exclusive creation, not a racy exists-then-overwrite check.
        if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {
            std::fputs("Cannot create new fixture; existing files are never overwritten.\n",stderr);
            return 3;
        }
        if(output.write(wire.data(),qint64(wire.size()))!=qint64(wire.size()) || !output.flush())
            return 3;
        return 0;
    }
    if(args.size()!=6 || args[1]!="verify" || args[2]!="--input" || args[4]!="--now")
        return usage();
    const auto timestamp=args[5].toStdString();
    std::int64_t now=0;
    const auto parsed=std::from_chars(timestamp.data(),timestamp.data()+timestamp.size(),now);
    if(timestamp.empty() || now<0 || parsed.ec!=std::errc{}
       || parsed.ptr!=timestamp.data()+timestamp.size())
        return usage();
    constexpr qint64 limit=256*1024;
    QFile input(args[3]);
    if(!input.open(QIODevice::ReadOnly) || input.size()>limit) return 3;
    const auto wire=input.read(limit+1);
    if(input.error()!=QFileDevice::NoError || wire.size()>limit || !input.atEnd()) return 3;
    const std::string_view bytes(wire.constData(),size_t(wire.size()));
    auto context=update_fixture::context();
    context.now=now;
    const auto result=zzlogg::update::verifyManifest(bytes,context);
    if(!result.value) {
        std::fprintf(stderr,"Fixture rejected (verification error %d).\n",int(result.error));
        return 4;
    }
    // Every successful fixture verification also checks the production boundary.
    context.environment=zzlogg::update::TrustEnvironment::Production;
    context.keys[0].purpose=zzlogg::update::KeyPurpose::Production;
    const auto production=zzlogg::update::verifyManifest(bytes,context);
    if(production.value || production.error!=zzlogg::update::VerificationError::TrustInvalid)
        return 5;
    std::puts("Test fixture verified; production trust correctly rejected it.");
    return 0;
}
