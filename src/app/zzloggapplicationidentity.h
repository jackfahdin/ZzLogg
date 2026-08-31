#ifndef ZZLOGG_APPLICATION_IDENTITY_H
#define ZZLOGG_APPLICATION_IDENTITY_H

#include <QString>

class QApplication;

void prepareZzLoggApplicationIdentity();
bool applyZzLoggApplicationIcon( QApplication& app, QString* error = nullptr );

#endif // ZZLOGG_APPLICATION_IDENTITY_H
