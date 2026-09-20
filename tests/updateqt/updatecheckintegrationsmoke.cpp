#include <QtTest>
#include <QAction>
#include <QLabel>
#include <QTemporaryDir>
#include "kloggapp.h"
#include "storagecontext.h"
#include "updatecheckdialog.h"
#include "zzlogg/updateqt/updateservice.h"
#include "zzlogg/updateqt/updatedownloadservice.h"
#include <QPushButton>
using namespace zzlogg::updateqt;
class UpdateIntegrationTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void menusShareOneApplicationServiceAndDialog() {
        auto& app=*qobject_cast<KloggApp*>(qApp);
        // This executable links an offline configuration fixture; the real
        // application's production feed is verified by separate tests.
        QVERIFY(productionFeedConfiguration().stableUrl.isEmpty());
        auto* first=app.newWindow(); auto* second=app.newWindow();
        auto* action=first->findChild<QAction*>("checkUpdatesAction"); QVERIFY(action);
        action->trigger();
        auto services=app.findChildren<UpdateService*>();
        QCOMPARE(services.size(),1);
        QCOMPARE(app.findChildren<UpdateDownloadService*>().size(),1);
        auto* service=services[0];
        QCOMPARE(service->snapshot().status,CheckStatus::NotConfigured);
        auto dialogs=[] {
            QList<UpdateCheckDialog*> result;
            for(auto* widget:QApplication::topLevelWidgets())
                if(auto* dialog=qobject_cast<UpdateCheckDialog*>(widget)) result.append(dialog);
            return result;
        };
        QCOMPARE(dialogs().size(),1);
        auto* dialog=dialogs()[0];
        auto* download=dialog->findChild<QPushButton*>("updateDownload");
        QVERIFY(download); QVERIFY(download->isHidden()); QVERIFY(!download->isEnabled());
        auto* downloadService=app.findChildren<UpdateDownloadService*>()[0];
        QCOMPARE(downloadService->snapshot().status,DownloadStatus::Idle);
        // Even a directly invoked UI signal must use the application's unconfigured check result.
        QVERIFY(QMetaObject::invokeMethod(dialog,"downloadRequested"));
        QCOMPARE(downloadService->snapshot().status,DownloadStatus::Unavailable);
        QVERIFY(downloadService->snapshot().verifiedPath.isEmpty());
        second->findChild<QAction*>("checkUpdatesAction")->trigger();
        QCOMPARE(dialogs().size(),1); QCOMPARE(dialogs()[0],dialog);
        QCOMPARE(downloadService->snapshot().status,DownloadStatus::Idle);
        QVERIFY(QMetaObject::invokeMethod(dialog,"downloadRequested"));
        Configuration::get().setUpdateChannel("preview");
        QVERIFY(QMetaObject::invokeMethod(second,"updatePreferencesChanged"));
        QCOMPARE(service->snapshot().channel,Channel::Preview);
        QCOMPARE(downloadService->snapshot().status,DownloadStatus::Idle);
        action->trigger();
        QCOMPARE(service->snapshot().status,CheckStatus::NotConfigured);
        QCOMPARE(app.findChildren<UpdateService*>().size(),1);
        QCOMPARE(app.findChildren<UpdateDownloadService*>().size(),1);
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCOMPARE(dialogs().size(),0);
        action->trigger(); QCOMPARE(dialogs().size(),1);
        app.destroyMainWindows();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCOMPARE(dialogs().size(),0);
    }
};
int main(int argc,char** argv) {
    QTemporaryDir root;
    KloggApp app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    if(!StorageContext::install({StorageMode::CustomDirectory,root.filePath("data"),root.filePath("locator.ini"),true},
        {root.filePath("program"),root.filePath("config"),root.filePath("user")})) return 2;
    Configuration::getSynced();
    UpdateIntegrationTest test;
    return QTest::qExec(&test,argc,argv);
}
#include "updatecheckintegrationsmoke.moc"
