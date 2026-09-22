#include <QtTest>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QAction>
#include <QTemporaryDir>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QTranslator>
#include <QDialogButtonBox>
#include <QScreen>
#include <QFontDatabase>
#include <QProgressBar>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <ZzFluentUI/ZzThemeController.h>
#include <ZzFluentUI/ZzFluentStyle.h>
#include "../update/fixturehelper.h"
#include "../updateqt/scriptednetwork.h"
#include "zzlogg/updateqt/updateservice.h"
#include "configuration.h"
#include "optionsdialog.h"
#include "mainwindow.h"
#include "storagecontext.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "updatecheckdialog.h"
using namespace zzlogg::updateqt;
namespace {
zzlogg::update::InstalledRelease installedRelease() {
    zzlogg::update::InstalledRelease result;
    result.version={26,9,0}; result.releaseSequence=1;
    result.osVersion={10,0,22631}; result.dataSchema=1;
    return result;
}
CheckSnapshot availableRelease(nlohmann::json payload=update_fixture::payload()) {
    auto verified=zzlogg::update::verifyManifest(update_fixture::envelope(payload.dump()),update_fixture::context());
    if(!verified.value) qFatal("UI fixture must have a verified release");
    return {CheckStatus::Available,Channel::Stable,verified.value,
        zzlogg::update::selectUpdate(*verified.value,installedRelease()),true,1800000000};
}
}
class UpdateCheckUiTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
private Q_SLOTS:
    void initTestCase() {
        QVERIFY(StorageContext::install({StorageMode::CustomDirectory,settings_.path(),settings_.filePath("storage.ini"),true}));
        Configuration::getSynced();
        RecentFiles::getSynced(); SavedSearches::getSynced();
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/consola.ttf");
        QStandardPaths::setTestModeEnabled(true);
    }
    void updateHintsReflectActualCapability() {
        UpdateCheckDialog dialog;
        auto* hint = dialog.findChild<QLabel*>("updateHint");
        QVERIFY(hint);
        dialog.setSnapshot({CheckStatus::NotConfigured, Channel::Stable, {}, {}, true});
        QVERIFY(hint->text().contains("online update service"));
        dialog.setSnapshot({CheckStatus::ReleaseInformation, Channel::Preview, {}, {}, true});
        QVERIFY(hint->text().contains("this installation"));
        dialog.setSnapshot(availableRelease());
        dialog.setDownloadSnapshot({DownloadStatus::Verified, 100, 100, {}, "cache/package"});
        QVERIFY(hint->text().contains("not available"));
        dialog.setUpdateExecutionAvailable(true);
        QVERIFY(!hint->text().contains("not available"));
        QVERIFY(hint->text().contains("install"));
        dialog.setHandoffState(UpdateCheckDialog::UpdateHandoffState::Waiting);
        QVERIFY(!dialog.findChild<QPushButton*>("updateSkip")->isEnabled());
    }
    void upToDateNeverMentionsInstallCapability() {
        UpdateCheckDialog dialog;
        auto* hint=dialog.findChild<QLabel*>("updateHint");
        QVERIFY(hint);
        dialog.setSnapshot({CheckStatus::UpToDate,Channel::Stable,{},{},true});
        // Mentioning install capability here reads as "a new version exists but cannot be installed".
        QVERIFY(hint->text().isEmpty());
        dialog.setSnapshot({CheckStatus::Idle,Channel::Stable,{},{},true});
        QVERIFY(hint->text().isEmpty());
    }
    void notConfiguredIsNotUpToDate() {
        UpdateCheckDialog dialog;
        dialog.setSnapshot({CheckStatus::NotConfigured,Channel::Stable,{},{},true});
        auto* status=dialog.findChild<QLabel*>("updateStatus");
        QVERIFY(status);
        QCOMPARE(status->text(),QString("Update service is not configured."));
        QVERIFY(!dialog.isModal());
    }
    // A missing download action or offering unsigned/informational packages breaks this contract.
    void downloadRequiresAvailableSelectedRelease() {
        UpdateCheckDialog dialog;
        auto* download=dialog.findChild<QPushButton*>("updateDownload");
        QVERIFY(download);
        auto snapshot=availableRelease();
        dialog.setSnapshot(snapshot);
        QVERIFY(!download->isHidden()); QVERIFY(download->isEnabled());
        QSignalSpy requested(&dialog,SIGNAL(downloadRequested()));
        QVERIFY(requested.isValid()); download->click(); QCOMPARE(requested.count(),1);
        for(auto status:{CheckStatus::NotConfigured,CheckStatus::ReleaseInformation,CheckStatus::Unsupported}) {
            snapshot.status=status; dialog.setSnapshot(snapshot);
            QVERIFY(download->isHidden()); QVERIFY(!download->isEnabled());
        }
        snapshot=availableRelease(); snapshot.decision.reset(); dialog.setSnapshot(snapshot);
        QVERIFY(download->isHidden()); QVERIFY(!download->isEnabled());
        snapshot=availableRelease(); snapshot.release.reset(); dialog.setSnapshot(snapshot);
        QVERIFY(download->isHidden()); QVERIFY(!download->isEnabled());
    }
    // Progress must not overflow and active downloads must block skip/duplicate actions.
    void downloadProgressAndRetryStates() {
        UpdateCheckDialog dialog; dialog.setSnapshot(availableRelease());
        dialog.setDownloadSnapshot({DownloadStatus::Downloading,25,100});
        auto* progress=dialog.findChild<QProgressBar*>("updateDownloadProgress");
        QVERIFY(progress); QCOMPARE(progress->value(),25);
        auto* download=dialog.findChild<QPushButton*>("updateDownload");
        QVERIFY(!download->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton*>("updateSkip")->isEnabled());
        auto* status=dialog.findChild<QLabel*>("updateDownloadStatus");
        QVERIFY(status->text().contains("25 / 100 bytes (25%)"));
        QSignalSpy cancelled(&dialog,&UpdateCheckDialog::downloadCancelRequested);
        dialog.findChild<QPushButton*>("updateCancel")->click(); QCOMPARE(cancelled.count(),1);
        dialog.setDownloadSnapshot({DownloadStatus::Downloading,3'000'000'000,4'000'000'000});
        QCOMPARE(progress->value(),75); QVERIFY(status->text().contains("3000000000 / 4000000000"));
        dialog.setDownloadSnapshot({DownloadStatus::Downloading,0,0}); QCOMPARE(progress->maximum(),0);
        for(auto terminal:{DownloadStatus::Failed,DownloadStatus::Cancelled}) {
            dialog.setDownloadSnapshot({terminal,0,0,DownloadError::Network});
            QVERIFY(download->isEnabled()); QVERIFY(!dialog.findChild<QPushButton*>("updateCancel")->isEnabled());
            QCOMPARE(download->text(),QString("Retry download"));
        }
        dialog.setDownloadSnapshot({DownloadStatus::Verified,100,100,{},"cache/package"});
        QCOMPARE(progress->value(),100); QVERIFY(!download->isEnabled());
        QCOMPARE(status->text(),QString("Download verified.\n100 / 100 bytes (100%)"));
        dialog.setDownloadSnapshot({DownloadStatus::Unavailable}); QVERIFY(!download->isEnabled());
    }
    // Network progress must preserve the user's place/selection in the release notes.
    void progressPreservesReleaseNotesSelection() {
        UpdateCheckDialog dialog; dialog.setSnapshot(availableRelease());
        auto* notes=dialog.findChild<QPlainTextEdit*>("updateNotes");
        auto cursor=notes->textCursor(); cursor.setPosition(0);
        cursor.setPosition(4,QTextCursor::KeepAnchor); notes->setTextCursor(cursor);
        QCOMPARE(notes->textCursor().selectedText(),QString("Test"));
        dialog.setDownloadSnapshot({DownloadStatus::Downloading,25,100});
        QCOMPARE(notes->textCursor().selectedText(),QString("Test"));
    }
    // Real service callbacks after close or parent destruction must never reach the retired dialog.
    void closingDownloadCancelsBeforeLateReply() {
        const auto context=update_fixture::context();
        FeedConfiguration config{"https://updates.example.invalid/stable","https://updates.example.invalid/preview",
            context.keys,context.allowedHosts,context.buildTime,context.environment};
        for(bool destroyParent:{false,true}) {
            QTemporaryDir cache; auto* network=new ScriptedNetworkManager;
            NetworkScript hanging; hanging.hang=true; network->scripts.push_back(hanging);
            UpdateDownloadService service(config,installedRelease(),cache.path(),[]{return 1800000000;},nullptr,[&]{return network;});
            auto* parent=new QWidget;
            QPointer<UpdateCheckDialog> dialog=new UpdateCheckDialog(parent);
            dialog->setSnapshot(availableRelease());
            int deliveries=0;
            connect(&service,&UpdateDownloadService::snapshotChanged,this,[&] {
                if(dialog) { ++deliveries; dialog->setDownloadSnapshot(service.snapshot()); }
            });
            connect(dialog,&UpdateCheckDialog::downloadRequested,&service,[&]{service.requestDownload(availableRelease());});
            connect(dialog,&UpdateCheckDialog::closing,this,[&]{dialog.clear();});
            connect(dialog,&UpdateCheckDialog::downloadCancelRequested,&service,&UpdateDownloadService::cancel);
            dialog->findChild<QPushButton*>("updateDownload")->click();
            QCOMPARE(service.snapshot().status,DownloadStatus::Downloading); QCOMPARE(deliveries,1);
            QSignalSpy changed(&service,&UpdateDownloadService::snapshotChanged);
            if(destroyParent) delete parent; else { dialog->show(); dialog->close(); }
            QVERIFY(!dialog); QCOMPARE(service.snapshot().status,DownloadStatus::Cancelled);
            QCoreApplication::processEvents(); QCOMPARE(changed.count(),1); QCOMPARE(deliveries,1);
            if(!destroyParent) delete parent;
        }
    }
    // Retry must use the saved signed release, recheck expiry, and only show verified bytes on success.
    void realDownloadRetryRevalidatesSavedRelease() {
        const auto context=update_fixture::context();
        FeedConfiguration config{"https://updates.example.invalid/stable","https://updates.example.invalid/preview",
            context.keys,context.allowedHosts,context.buildTime,context.environment};
        auto payload=update_fixture::payload(); payload["artifacts"][0]["size"]="7";
        payload["artifacts"][0]["sha256"]=QCryptographicHash::hash("package",QCryptographicHash::Sha256).toHex().toStdString();
        const auto saved=availableRelease(payload);
        for(bool expire:{false,true}) {
            QTemporaryDir cache; qint64 now=1800000000;
            auto* network=new ScriptedNetworkManager;
            NetworkScript fail; fail.status=503;
            NetworkScript good; good.body="package"; network->scripts={fail,good};
            UpdateDownloadService service(config,installedRelease(),cache.path(),[&]{return now;},nullptr,[&]{return network;});
            UpdateCheckDialog dialog; dialog.setSnapshot(saved);
            connect(&dialog,&UpdateCheckDialog::downloadRequested,&service,[&]{service.requestDownload(saved);});
            connect(&service,&UpdateDownloadService::snapshotChanged,&dialog,[&]{dialog.setDownloadSnapshot(service.snapshot());});
            auto* download=dialog.findChild<QPushButton*>("updateDownload");
            QCOMPARE(service.snapshot().status,DownloadStatus::Idle); QVERIFY(network->requests.isEmpty());
            download->click(); QTRY_COMPARE(service.snapshot().status,DownloadStatus::Failed);
            QVERIFY(download->isEnabled());
            if(expire) now=1800003600;
            download->click();
            QTRY_COMPARE(service.snapshot().status,expire ? DownloadStatus::Unavailable : DownloadStatus::Verified);
            QCOMPARE(network->requests.size(),expire ? 1 : 2);
            if(expire) QVERIFY(service.snapshot().verifiedPath.isEmpty());
            else {
                QFile file(service.snapshot().verifiedPath); QVERIFY(file.open(QIODevice::ReadOnly));
                QCOMPARE(file.readAll(),QByteArray("package"));
                QCOMPARE(dialog.findChild<QProgressBar*>("updateDownloadProgress")->value(),100);
            }
        }
    }
    void closingManualCheckCancelsOnce() {
        UpdateCheckDialog dialog;
        QSignalSpy cancelled(&dialog,&UpdateCheckDialog::cancelRequested);
        dialog.setSnapshot({CheckStatus::Checking,Channel::Stable,{},{},true});
        dialog.show();
        dialog.close();
        QCOMPARE(cancelled.count(),1);
    }
    void destroyingParentCancelsManualCheck() {
        auto* parent=new QWidget;
        auto* dialog=new UpdateCheckDialog(parent);
        QSignalSpy cancelled(dialog,&UpdateCheckDialog::cancelRequested);
        dialog->setSnapshot({CheckStatus::Checking,Channel::Stable,{},{},true});
        delete parent;
        QCOMPARE(cancelled.count(),1);
    }
    void checkingShowsCancelWhileActive() {
        UpdateCheckDialog dialog;
        dialog.setSnapshot({CheckStatus::Checking,Channel::Stable,{},{},true});
        auto* cancel=dialog.findChild<QPushButton*>("updateCancel");
        QVERIFY(cancel);
        QVERIFY(cancel->isVisible());
        QVERIFY(cancel->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton*>("updateCheck"));
    }
    void parentDestructionCancelsServiceBeforeLateReply() {
        QTemporaryDir directory;
        auto store=std::make_shared<UpdateStateStore>(directory.filePath("state.json"));
        const auto context=update_fixture::context();
        FeedConfiguration config{"https://updates.example.invalid/stable",
            "https://updates.example.invalid/preview",context.keys,context.allowedHosts,
            context.buildTime,context.environment};
        auto* network=new ScriptedNetworkManager;
        NetworkScript script; script.hang=true; network->scripts.push_back(script);
        UpdateService service(config,store,{},[]{return 1800000000;},nullptr,[&]{return network;});
        auto* parent=new QWidget;
        QPointer<UpdateCheckDialog> dialog=new UpdateCheckDialog(parent);
        bool dismissed=false;
        connect(dialog,&UpdateCheckDialog::closing,this,[&]{dismissed=true; dialog.clear();});
        connect(dialog,&UpdateCheckDialog::cancelRequested,&service,&UpdateService::cancel);
        connect(&service,&UpdateService::snapshotChanged,this,[&]{
            if(dialog) dialog->setSnapshot(service.snapshot());
        });
        service.requestCheck(Channel::Stable,CheckOrigin::Manual);
        QCOMPARE(service.snapshot().status,CheckStatus::Checking);
        QCOMPARE(network->requests.size(),1);
        QSignalSpy changed(&service,&UpdateService::snapshotChanged);
        delete parent;
        QVERIFY(dismissed); QVERIFY(!dialog);
        QCOMPARE(service.snapshot().status,CheckStatus::Cancelled);
        QCOMPARE(changed.count(),1);
        QCoreApplication::processEvents();
        QCOMPARE(changed.count(),1);
        QVERIFY(!store->read(Channel::Stable).value->accepted);
    }
    void settingsHasIndependentChannelPage() {
        try {
        OptionsDialog dialog;
        auto* channel=dialog.findChild<QComboBox*>("updateChannel");
        QVERIFY(channel); QCOMPARE(channel->count(),2);
        QVERIFY(dialog.findChild<QPushButton*>("updateCheckNow"));
        } catch(const std::exception& error) { QFAIL(error.what()); }
    }
    void menuOffersCheckAction() {
        MainWindow window{WindowSession{std::make_shared<Session>(),"update-ui",0}};
        auto* action=window.findChild<QAction*>("checkUpdatesAction");
        QVERIFY(action);
        QSignalSpy requested(&window,SIGNAL(checkUpdatesRequested()));
        action->trigger(); QCOMPARE(requested.count(),1);
    }
    void displayChangesDoNotInventCheckTime() {
        UpdateCheckDialog dialog;
        CheckSnapshot snapshot{CheckStatus::UpToDate,Channel::Stable,{},{},true};
        snapshot.checkedAt=1800000000;
        dialog.setSnapshot(snapshot);
        auto* details=dialog.findChild<QLabel*>("updateDetails");
        QVERIFY(details->text().contains(QDateTime::fromSecsSinceEpoch(1800000000).toString(Qt::ISODate)));
        const auto text=details->text(); snapshot.presentToUser=false; dialog.setSnapshot(snapshot);
        QCOMPARE(details->text(),text);
        dialog.setSnapshot({CheckStatus::Idle,Channel::Preview});
        QVERIFY(details->text().isEmpty());
    }
    void applyCancelAndImmediateCheckRespectSavedChannel() {
        auto& config=Configuration::get();
        config.setUpdateChannel("stable"); config.setVersionCheckingEnabled(true); config.save();
        {
            OptionsDialog dialog;
            auto* channel=dialog.findChild<QComboBox*>("updateChannel");
            auto* automatic=dialog.findChild<QCheckBox*>("updateAutomatic");
            QVERIFY(channel && automatic);
            channel->setCurrentIndex(1); automatic->setChecked(false);
            QSignalSpy requested(&dialog,SIGNAL(checkUpdatesRequested()));
            dialog.findChild<QPushButton*>("updateCheckNow")->click();
            QCOMPARE(requested.count(),1); QCOMPARE(config.updateChannel(),QString("stable"));
            QVERIFY(config.versionCheckingEnabled());
            dialog.reject();
        }
        QCOMPARE(config.updateChannel(),QString("stable")); QVERIFY(config.versionCheckingEnabled());
        OptionsDialog dialog;
        dialog.findChild<QComboBox*>("updateChannel")->setCurrentIndex(1);
        dialog.findChild<QCheckBox*>("updateAutomatic")->setChecked(false);
        config.setLanguage(dialog.findChild<QComboBox*>("languageComboBox")->currentData().toString());
        dialog.buttonBox->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(Configuration::getSynced().updateChannel(),QString("preview"));
        QVERIFY(!Configuration::get().versionCheckingEnabled());
        QVERIFY(dialog.findChild<QLabel*>("updateAppliedChannel")->text().contains("Preview"));
    }
    void notesArePlainTextAndLanguageSwitchesLive() {
        auto payload=update_fixture::payload();
        payload["notes"]["en"]="<b>Not HTML</b> https://example.invalid";
        payload["notes"]["zh_CN"]="简体说明";
        payload["notes"]["zh_TW"]="繁體說明";
        auto verified=zzlogg::update::verifyManifest(update_fixture::envelope(payload.dump()),update_fixture::context());
        QVERIFY(verified.value);
        UpdateCheckDialog dialog;
        dialog.setSnapshot({CheckStatus::ReleaseInformation,Channel::Stable,verified.value,{},true});
        auto* notes=dialog.findChild<QPlainTextEdit*>("updateNotes"); QVERIFY(notes);
        const QStringList languages{"en","zh_CN","zh_TW"};
        const QStringList expected{"<b>Not HTML</b> https://example.invalid","简体说明","繁體說明"};
        const QStringList titles{"Check for updates","检查更新","檢查更新"};
        const QStringList downloadLabels{"Download update","下载更新","下載更新"};
        const QStringList verifiedLabels{"Download verified.",
            "下载已验证。","下載已驗證。"};
        for(int i=0;i<3;++i) {
            QTranslator translator;
            QVERIFY(translator.load(QString(ZZLOGG_UI_QM_DIR)+"/"+languages[i]+".qm"));
            Configuration::get().setLanguage(languages[i]);
            qApp->installTranslator(&translator); QCoreApplication::processEvents();
            QCOMPARE(notes->toPlainText(),expected[i]); QCOMPARE(dialog.windowTitle(),titles[i]);
            dialog.setDownloadSnapshot({DownloadStatus::Downloading,25,100});
            QCOMPARE(dialog.findChild<QPushButton*>("updateDownload")->text(),downloadLabels[i]);
            dialog.setDownloadSnapshot({DownloadStatus::Verified,100,100});
            QVERIFY(dialog.findChild<QLabel*>("updateDownloadStatus")->text().startsWith(verifiedLabels[i]));
            qApp->removeTranslator(&translator);
        }
    }
    // The quit-and-install action requires both the execution capability and a
    // fresh selected verified package; every check/selection/download change
    // clears the pushed capability and the default production state hides it.
    void installActionRequiresCapabilityAndFreshVerifiedPackage() {
        UpdateCheckDialog dialog;
        auto* install=dialog.findChild<QPushButton*>("updateInstall");
        QVERIFY(install);
        QVERIFY(install->isHidden()); QVERIFY(!install->isEnabled());
        QSignalSpy requested(&dialog,SIGNAL(installRequested()));
        QVERIFY(requested.isValid());
        dialog.setSnapshot(availableRelease());
        dialog.setDownloadSnapshot({DownloadStatus::Verified,100,100,{},"cache/package"});
        QVERIFY(install->isHidden()); QVERIFY(!install->isEnabled());
        dialog.setUpdateExecutionAvailable(true);
        QVERIFY(!install->isHidden()); QVERIFY(install->isEnabled());
        QCOMPARE(install->text(),QString("Quit and install update"));
        install->click(); QCOMPARE(requested.count(),1);
        auto checking=availableRelease(); checking.status=CheckStatus::Checking;
        dialog.setSnapshot(checking);
        QVERIFY(install->isHidden()); QVERIFY(!install->isEnabled());
        dialog.setSnapshot(availableRelease());
        QVERIFY(install->isHidden()); // capability stayed cleared
        dialog.setUpdateExecutionAvailable(true);
        QVERIFY(!install->isHidden()); QVERIFY(install->isEnabled());
        dialog.setDownloadSnapshot({DownloadStatus::Downloading,25,100});
        QVERIFY(install->isHidden()); QVERIFY(!install->isEnabled());
        dialog.setUpdateExecutionAvailable(true);
        QVERIFY(install->isHidden()); // no verified package while downloading
        auto info=availableRelease(); info.status=CheckStatus::ReleaseInformation;
        dialog.setSnapshot(info);
        dialog.setDownloadSnapshot({DownloadStatus::Verified,100,100,{},"cache/package"});
        dialog.setUpdateExecutionAvailable(true);
        QVERIFY(install->isHidden()); QVERIFY(!install->isEnabled());
        QCOMPARE(requested.count(),1);
    }
    // Preparing/waiting disable selection changes; close and ESC both cancel.
    void handoffStatesDisableSelectionChangesAndCloseCancels() {
        using S=UpdateCheckDialog::UpdateHandoffState;
        UpdateCheckDialog dialog;
        dialog.setSnapshot(availableRelease());
        dialog.setDownloadSnapshot({DownloadStatus::Verified,100,100,{},"cache/package"});
        dialog.setUpdateExecutionAvailable(true);
        auto* install=dialog.findChild<QPushButton*>("updateInstall");
        auto* download=dialog.findChild<QPushButton*>("updateDownload");
        auto* skip=dialog.findChild<QPushButton*>("updateSkip");
        auto* later=dialog.findChild<QPushButton*>("updateLater");
        auto* cancel=dialog.findChild<QPushButton*>("updateCancel");
        auto* status=dialog.findChild<QLabel*>("updateHandoffStatus");
        QVERIFY(status);
        dialog.show();
        dialog.setHandoffState(S::Preparing);
        QVERIFY(!download->isEnabled());
        QVERIFY(!skip->isEnabled()); QVERIFY(!later->isEnabled());
        QVERIFY(!install->isEnabled()); QVERIFY(!install->isHidden());
        QVERIFY(cancel->isEnabled()); QVERIFY(!cancel->isHidden());
        QCOMPARE(cancel->text(),QString("Cancel update"));
        QCOMPARE(status->text(),QString("Preparing the update. Your session is being saved..."));
        QSignalSpy cancelledSpy(&dialog,SIGNAL(installCancelRequested()));
        QVERIFY(cancelledSpy.isValid());
        QTest::keyClick(&dialog,Qt::Key_Escape);
        QCOMPARE(cancelledSpy.count(),1);
        QVERIFY(dialog.isVisible());
        dialog.setHandoffState(S::Waiting);
        QCOMPARE(status->text(),QString("Closing ZzLogg and starting the update..."));
        cancel->click(); QCOMPARE(cancelledSpy.count(),2);
        QVERIFY(dialog.isVisible());
        dialog.setHandoffState(S::Cancelled);
        QVERIFY(install->isEnabled());
        QCOMPARE(status->text(),QString("Update cancelled. Your session is unchanged."));
        dialog.close();
        QVERIFY(!dialog.isVisible());
    }
    // Failure texts are the controller-to-UI error mapping, never raw detail.
    void handoffFailureStatesShowMappedTexts() {
        using E=UpdateCheckDialog::UpdateHandoffError;
        using S=UpdateCheckDialog::UpdateHandoffState;
        UpdateCheckDialog dialog;
        dialog.setSnapshot(availableRelease());
        auto* status=dialog.findChild<QLabel*>("updateHandoffStatus");
        QVERIFY(status);
        const QList<QPair<E,QString>> cases{
            {E::Preparation,QString("Unable to prepare the update. Your session is unchanged.")},
            {E::Blocked,QString("Another ZzLogg instance is active in this installation. Close it and try again.")},
            {E::Abandoned,QString("A previous update did not finish cleanly. Wait a moment and try again.")},
            {E::Unavailable,QString("The installation directory could not be verified. The update was not started.")},
            {E::Helper,QString("The update helper could not be started. Your session is unchanged.")},
            {E::Commit,QString("The update could not be committed. Your session is unchanged.")},
            {E::ApprovalDeclined,QString("Administrator approval was declined. No changes were made.")},
            {E::Closed,QString("Updates are not available for this installation.")},
        };
        for(const auto& entry:cases) {
            dialog.setHandoffState(S::Failed,entry.first);
            QCOMPARE(status->text(),entry.second);
            QVERIFY(!status->isHidden());
            dialog.setHandoffState(S::Idle);
            QVERIFY(status->text().isEmpty());
            QVERIFY(status->isHidden());
        }
    }
    // Handoff strings switch live between English, simplified and traditional.
    void handoffStringsTranslateLive() {
        using E=UpdateCheckDialog::UpdateHandoffError;
        using S=UpdateCheckDialog::UpdateHandoffState;
        UpdateCheckDialog dialog;
        dialog.setSnapshot(availableRelease());
        auto* install=dialog.findChild<QPushButton*>("updateInstall");
        auto* status=dialog.findChild<QLabel*>("updateHandoffStatus");
        const QStringList languages{"en","zh_CN","zh_TW"};
        const QStringList installLabels{"Quit and install update","退出并安装更新","結束並安裝更新"};
        const QStringList preparingTexts{"Preparing the update. Your session is being saved...",
            "正在准备更新，会话正在保存...","正在準備更新，工作階段正在儲存..."};
        const QStringList cancelledTexts{"Update cancelled. Your session is unchanged.",
            "更新已取消，会话未更改。","更新已取消，工作階段未變更。"};
        const QStringList abandonedTexts{"A previous update did not finish cleanly. Wait a moment and try again.",
            "上一次更新未正常完成。请稍候再试。","上一次更新未正常完成。請稍候再試。"};
        for(int i=0;i<3;++i) {
            QTranslator translator;
            QVERIFY(translator.load(QString(ZZLOGG_UI_QM_DIR)+"/"+languages[i]+".qm"));
            Configuration::get().setLanguage(languages[i]);
            qApp->installTranslator(&translator); QCoreApplication::processEvents();
            dialog.setDownloadSnapshot({DownloadStatus::Verified,100,100,{},"cache/package"});
            dialog.setUpdateExecutionAvailable(true);
            QCOMPARE(install->text(),installLabels[i]);
            dialog.setHandoffState(S::Preparing);
            QCOMPARE(status->text(),preparingTexts[i]);
            dialog.setHandoffState(S::Cancelled);
            QCOMPARE(status->text(),cancelledTexts[i]);
            dialog.setHandoffState(S::Failed,E::Abandoned);
            QCOMPARE(status->text(),abandonedTexts[i]);
            dialog.setHandoffState(S::Idle);
            qApp->removeTranslator(&translator);
        }
    }
    void longNotesKeepFooterVisibleWithBothThemes() {
        auto payload=update_fixture::payload();
        for(const auto* language:{"en","zh_CN","zh_TW"}) payload["notes"][language]=std::string(16000,'W');
        auto snapshot=availableRelease(payload);
        Configuration::get().setLanguage("en");
        auto* theme=new ZzFluentUI::ZzThemeController(qApp);
        qApp->setStyle(new ZzFluentUI::ZzFluentStyle(theme));
        for(const auto* language:{"en","zh_CN","zh_TW"}) {
          QTranslator translator;
          QVERIFY(translator.load(QString(ZZLOGG_UI_QM_DIR)+"/"+language+".qm"));
          Configuration::get().setLanguage(language); qApp->installTranslator(&translator);
          for(auto mode:{ZzFluentUI::ZzThemeMode::Light,ZzFluentUI::ZzThemeMode::Dark}) {
            theme->setMode(mode);
          for(auto downloadStatus:{DownloadStatus::Idle,DownloadStatus::Downloading,DownloadStatus::Cancelled,DownloadStatus::Verified}) {
            UpdateCheckDialog dialog;
            dialog.setSnapshot(snapshot);
            dialog.setDownloadSnapshot({downloadStatus,downloadStatus==DownloadStatus::Verified ? 100 : 25,100});
            dialog.setUpdateExecutionAvailable(true); // also covers the install button containment
            dialog.resize(600,480); dialog.show(); QCoreApplication::processEvents();
            for(auto* button:dialog.findChildren<QPushButton*>()) if(button->isVisible())
                QVERIFY(dialog.rect().contains(QRect(button->mapTo(&dialog,QPoint()),button->size())));
            if(downloadStatus==DownloadStatus::Downloading || downloadStatus==DownloadStatus::Verified) {
                auto* progress=dialog.findChild<QProgressBar*>("updateDownloadProgress");
                QVERIFY(!progress->visibleRegion().isEmpty());
            }
            QVERIFY(dialog.height()<=720); QVERIFY(dialog.width()<=1232);
            const auto captured=dialog.grab();
            QVERIFY(!captured.isNull());
            QVERIFY(captured.save(QString(ZZLOGG_UI_QM_DIR)+
                (mode==ZzFluentUI::ZzThemeMode::Dark ? "/update-download-dark-" : "/update-download-light-")+language
                +QString("-%1-%2-%3.png").arg(int(downloadStatus)).arg(QGuiApplication::platformName())
                    .arg(qRound(dialog.devicePixelRatioF()*100))));
          }
          }
          qApp->removeTranslator(&translator);
        }
    }
};
QTEST_MAIN(UpdateCheckUiTest)
#include "updatecheckuitest.moc"
