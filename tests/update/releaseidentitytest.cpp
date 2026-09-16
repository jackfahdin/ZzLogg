#include "zzlogg/update/releaseidentity.h"

#include <QtTest>

using namespace zzlogg::update;

class ReleaseIdentityTest : public QObject {
    Q_OBJECT

private slots:
    void developmentBuildHasNoReleaseIdentity()
    {
        QVERIFY(!compiledReleaseIdentity());
    }
};

QTEST_GUILESS_MAIN(ReleaseIdentityTest)
#include "releaseidentitytest.moc"
