#ifndef KLOGG_APPLICATIONUPDATESESSION_H
#define KLOGG_APPLICATIONUPDATESESSION_H

#include "applicationupdatehandoff.h"

// Production session-factory inputs. A non-null factory is returned only when
// every condition holds; otherwise executionAvailable() stays false and the UI
// does not offer quit-and-install.
struct ApplicationUpdateSessionInputs {
    QString registeredInstallRoot;
    QString verifiedPackagePath;
    quint64 packageSize = 0;
    QString packageSha256; // 64 lowercase hex digits
    bool releaseIdentityAvailable = false;
};

ApplicationUpdateHandoff::SessionFactory makeUpdateSessionFactory(
    const ApplicationUpdateSessionInputs& inputs);

#endif // KLOGG_APPLICATIONUPDATESESSION_H
