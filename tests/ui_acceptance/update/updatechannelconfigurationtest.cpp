#include "configuration.h"

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QSettings settings(directory.filePath("fresh.ini"), QSettings::IniFormat);
    const QString expected = QString::fromLatin1(TEST_DEFAULT_UPDATE_CHANNEL);
    Configuration configuration;
    if (configuration.updateChannel() != expected) {
        qCritical() << "New configuration chose" << configuration.updateChannel() << "instead of" << expected;
        return 1;
    }
    configuration.retrieveFromStorage(settings);
    if (configuration.updateChannel() != expected) {
        qCritical() << "Missing persisted channel did not use the build default";
        return 2;
    }
    configuration.saveToStorage(settings);
    if (settings.value("updates.channel").toString() != expected) return 3;

    // A saved preference remains explicit; defaults must not silently override it.
    const QString selected = expected == "preview" ? QStringLiteral("stable") : QStringLiteral("preview");
    settings.setValue("updates.channel", selected);
    configuration.retrieveFromStorage(settings);
    if (configuration.updateChannel() != selected) return 4;
    settings.remove("updates.channel");
    configuration.retrieveFromStorage(settings);
    if (configuration.updateChannel() != expected) return 5;
    return 0;
}
