#pragma once
#include "zzlogg/update/manifest.h"
#include <QString>

namespace zzlogg::updateqt {
// Derives only from the operating system CacheLocation; does not create directories.
QString updateCachePath(update::TrustEnvironment environment=update::TrustEnvironment::Production);
}
