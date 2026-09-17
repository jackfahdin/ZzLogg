#include "updatecheckdialog.h"
#include "configuration.h"
#include "klogg_version.h"
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
using namespace zzlogg::updateqt;
namespace {
QLabel* label(QWidget* parent) {
    auto* result=new QLabel(parent);
    result->setTextFormat(Qt::PlainText); result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return result;
}
}
UpdateCheckDialog::UpdateCheckDialog(QWidget* parent):QDialog(parent)
{
    setObjectName("updateCheckDialog");
    setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    auto* outer=new QVBoxLayout(this); outer->setContentsMargins(20,20,20,16); outer->setSpacing(16);
    auto* scroll=new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto* content=new QWidget(scroll); auto* body=new QVBoxLayout(content); body->setSpacing(14);
    identity_=label(content); status_=label(content); details_=label(content); hint_=label(content);
    downloadStatus_=label(this); downloadStatus_->setObjectName("updateDownloadStatus");
    handoffStatus_=label(this); handoffStatus_->setObjectName("updateHandoffStatus");
    progress_=new QProgressBar(this); progress_->setObjectName("updateDownloadProgress");
    status_->setObjectName("updateStatus"); details_->setObjectName("updateDetails");
    auto font=status_->font(); font.setBold(true); font.setPointSize(font.pointSize()+2); status_->setFont(font);
    notes_=new QPlainTextEdit(content); notes_->setReadOnly(true); notes_->setObjectName("updateNotes");
    notes_->setMinimumHeight(120); notes_->setFrameShape(QFrame::NoFrame);
    body->addWidget(identity_); body->addWidget(status_); body->addWidget(details_);
    body->addWidget(notes_,1); body->addWidget(hint_);
    scroll->setWidget(content); outer->addWidget(scroll,1);
    outer->addWidget(downloadStatus_); outer->addWidget(progress_); outer->addWidget(handoffStatus_);
    auto* footer=new QHBoxLayout; outer->addLayout(footer);
    check_=new QPushButton(this); cancel_=new QPushButton(this); later_=new QPushButton(this);
    skip_=new QPushButton(this); close_=new QPushButton(this);
    download_=new QPushButton(this); download_->setObjectName("updateDownload");
    install_=new QPushButton(this); install_->setObjectName("updateInstall");
    check_->setObjectName("updateCheck"); cancel_->setObjectName("updateCancel"); close_->setObjectName("updateClose");
    skip_->setObjectName("updateSkip"); later_->setObjectName("updateLater");
    for(auto* button:{check_,download_,install_,cancel_,later_,skip_,close_}) { button->setAutoDefault(false); footer->addWidget(button); }
    connect(check_,&QPushButton::clicked,this,&UpdateCheckDialog::checkRequested);
    connect(download_,&QPushButton::clicked,this,&UpdateCheckDialog::downloadRequested);
    connect(install_,&QPushButton::clicked,this,[this] {
        // Defense in depth: capability and a fresh verified package are
        // rechecked at click time, not only at refresh time.
        if(installAvailable()) Q_EMIT installRequested();
    });
    connect(cancel_,&QPushButton::clicked,this,[this] {
        if(handoffState_==UpdateHandoffState::Preparing || handoffState_==UpdateHandoffState::Waiting)
            Q_EMIT installCancelRequested();
        else if(downloadSnapshot_.status==DownloadStatus::Downloading) Q_EMIT downloadCancelRequested();
        else Q_EMIT cancelRequested();
    });
    connect(skip_,&QPushButton::clicked,this,&UpdateCheckDialog::skipRequested);
    connect(later_,&QPushButton::clicked,this,&UpdateCheckDialog::reject);
    connect(close_,&QPushButton::clicked,this,&UpdateCheckDialog::reject);
    resize(600,480); refresh();
}
UpdateCheckDialog::~UpdateCheckDialog()
{
    if(!dismissed_) {
        Q_EMIT closing();
        if(handoffState_==UpdateHandoffState::Preparing || handoffState_==UpdateHandoffState::Waiting)
            Q_EMIT installCancelRequested();
        if(snapshot_.status==CheckStatus::Checking && snapshot_.presentToUser) Q_EMIT cancelRequested();
        if(downloadSnapshot_.status==DownloadStatus::Downloading) Q_EMIT downloadCancelRequested();
    }
}
void UpdateCheckDialog::setSnapshot(const CheckSnapshot& snapshot)
{
    snapshot_=snapshot;
    checkedAt_=snapshot.checkedAt>0 ? QDateTime::fromSecsSinceEpoch(snapshot.checkedAt) : QDateTime{};
    updateExecutionAvailable_=false; // any selection/check change clears the pushed capability
    refresh();
}
void UpdateCheckDialog::setDownloadSnapshot(const DownloadSnapshot& snapshot)
{
    downloadSnapshot_=snapshot;
    updateExecutionAvailable_=false; // any download change clears the pushed capability
    refresh();
}
void UpdateCheckDialog::setUpdateExecutionAvailable(bool available)
{
    updateExecutionAvailable_=available;
    refresh();
}
void UpdateCheckDialog::setHandoffState(UpdateHandoffState state, UpdateHandoffError error)
{
    handoffState_=state;
    handoffError_=error;
    refresh();
}
bool UpdateCheckDialog::installAvailable() const
{
    // Both the pushed execution capability and a fresh selected verified
    // package are required; the Verified download state alone is never enough.
    const bool downloadable=snapshot_.status==CheckStatus::Available && snapshot_.release
        && snapshot_.decision
        && snapshot_.decision->status==zzlogg::update::DecisionStatus::Available
        && snapshot_.decision->artifact;
    return updateExecutionAvailable_ && downloadable
        && downloadSnapshot_.status==DownloadStatus::Verified;
}
void UpdateCheckDialog::refresh()
{
    setWindowTitle(tr("Check for updates"));
    const auto channel=snapshot_.channel==Channel::Stable ? tr("Stable") : tr("Preview");
    identity_->setText(tr("Current version: %1\nChannel: %2").arg(kloggVersion(),channel));
    QString text;
    switch(snapshot_.status) {
    case CheckStatus::Idle: text=tr("Ready to check for updates."); break;
    case CheckStatus::NotConfigured: text=tr("Update service is not configured."); break;
    case CheckStatus::Checking: text=tr("Checking for updates..."); break;
    case CheckStatus::Cancelled: text=tr("Update check cancelled."); break;
    case CheckStatus::UpToDate: text=tr("You are using the latest version."); break;
    case CheckStatus::Available: text=tr("A new version is available."); break;
    case CheckStatus::ReleaseInformation: text=tr("Verified release information (installation identity unavailable)."); break;
    case CheckStatus::Unsupported: text=tr("This release is not compatible with this installation."); break;
    case CheckStatus::NetworkError: text=tr("Unable to retrieve the update manifest. Please try again later."); break;
    case CheckStatus::VerificationFailed: text=tr("Update verification failed. Check the system clock or try again later."); break;
    case CheckStatus::StateInvalid: text=tr("Local update state cannot be read. It has not been reset."); break;
    case CheckStatus::StateBusy: text=tr("Another process is updating the local state. Please try again later."); break;
    case CheckStatus::StateWriteFailed: text=tr("Unable to save update state. No update has been accepted."); break;
    }
    status_->setText(text);
    QString details=checkedAt_.isValid() ? tr("Last check: %1").arg(checkedAt_.toString(Qt::ISODate)) : QString{};
    QString notes;
    if(snapshot_.release) {
        const auto& release=snapshot_.release->manifest();
        const auto version=QString("%1.%2.%3").arg(release.version.year,2,10,QChar('0'))
            .arg(release.version.month,2,10,QChar('0')).arg(release.version.patch,2,10,QChar('0'));
        details+=QStringLiteral("\n")+tr("Release version: %1").arg(version);
        const auto language=Configuration::get().language();
        const size_t index=language=="zh_CN" ? 1 : language=="zh_TW" ? 2 : 0;
        notes=QString::fromStdString(release.notes[index]);
        if(snapshot_.decision && snapshot_.decision->artifact) {
            const auto& artifact=*snapshot_.decision->artifact;
            details+=QStringLiteral("\n")+tr("Package: %1 (%2 bytes)")
                .arg(artifact.distribution==zzlogg::update::Distribution::Portable ? tr("Portable") : tr("Installer"))
                .arg(qulonglong(artifact.size));
        }
    }
    details_->setText(details);
    if(notes_->toPlainText()!=notes) notes_->setPlainText(notes);
    notes_->setVisible(snapshot_.release.has_value());
    hint_->setText(tr("This version supports update checks and verified downloads. Installation is not available yet."));
    check_->setText(tr("Check again")); cancel_->setText(tr("Cancel check"));
    later_->setText(tr("Remind me later")); skip_->setText(tr("Skip this version")); close_->setText(tr("Close"));
    const bool checking=snapshot_.status==CheckStatus::Checking;
    const bool downloading=downloadSnapshot_.status==DownloadStatus::Downloading;
    const bool downloadable=snapshot_.status==CheckStatus::Available && snapshot_.release && snapshot_.decision
        && snapshot_.decision->status==zzlogg::update::DecisionStatus::Available && snapshot_.decision->artifact;
    const bool retry=downloadSnapshot_.status==DownloadStatus::Failed || downloadSnapshot_.status==DownloadStatus::Cancelled;
    download_->setText(retry ? tr("Retry download") : tr("Download update"));
    download_->setVisible(downloadable);
    download_->setEnabled(downloadable && !downloading && downloadSnapshot_.status!=DownloadStatus::Verified
        && downloadSnapshot_.status!=DownloadStatus::Unavailable);
    check_->setEnabled(!checking && !downloading);
    cancel_->setVisible(checking || downloading); cancel_->setEnabled(checking || downloading);
    if(downloading) cancel_->setText(tr("Cancel download"));
    skip_->setVisible(snapshot_.release.has_value() && !downloading);
    later_->setVisible(snapshot_.release.has_value() && !downloading);
    skip_->setEnabled(!checking && !downloading); later_->setEnabled(!checking && !downloading);
    QString downloadText;
    switch(downloadSnapshot_.status) {
    case DownloadStatus::Idle: break;
    case DownloadStatus::Downloading: downloadText=tr("Downloading update..."); break;
    case DownloadStatus::Verified: downloadText=tr("Download verified. Installation is not available yet."); break;
    case DownloadStatus::Cancelled: downloadText=tr("Download cancelled. You can retry."); break;
    case DownloadStatus::Unavailable: downloadText=tr("This download is no longer available. Check for updates again."); break;
    case DownloadStatus::Failed:
        switch(downloadSnapshot_.error.value_or(DownloadError::Network)) {
        case DownloadError::InsufficientSpace: downloadText=tr("Not enough disk space. Free some space and retry."); break;
        case DownloadError::Busy: downloadText=tr("Another process is downloading this package. Please retry later."); break;
        case DownloadError::InvalidPath: case DownloadError::WriteFailed:
            downloadText=tr("Unable to write the update cache. Check disk access and retry."); break;
        case DownloadError::HashMismatch: case DownloadError::SizeMismatch:
            downloadText=tr("Package verification failed. Please retry the download."); break;
        default: downloadText=tr("Unable to download the update. Please retry."); break;
        }
        break;
    }
    const bool showProgress=downloading || downloadSnapshot_.status==DownloadStatus::Verified;
    progress_->setVisible(showProgress);
    if(showProgress) {
        const auto received=std::max(qint64(0),downloadSnapshot_.received);
        const auto total=std::max(qint64(0),downloadSnapshot_.total);
        const auto percent=total>0 ? int(std::clamp(100.0L*received/total,0.0L,100.0L)) : 0;
        progress_->setRange(0,total>0 ? 100 : 0); progress_->setValue(percent);
        downloadText+=QStringLiteral("\n")+(total>0
            ? tr("%1 / %2 bytes (%3%)").arg(received).arg(total).arg(percent)
            : tr("%1 bytes received").arg(received));
    }
    downloadStatus_->setText(downloadText); downloadStatus_->setVisible(!downloadText.isEmpty());
    // Restricted handoff presentation: the install action stays hidden unless
    // the backend pushed the capability and a fresh verified package exists.
    const bool handoffBusy=handoffState_==UpdateHandoffState::Preparing
        || handoffState_==UpdateHandoffState::Waiting;
    install_->setText(tr("Quit and install update"));
    install_->setVisible(installAvailable() || handoffBusy);
    install_->setEnabled(installAvailable() && !handoffBusy);
    if(handoffBusy) {
        // Preparing/waiting freezes every selection-changing action; the only
        // offered operation is cancelling the handoff.
        check_->setEnabled(false); download_->setEnabled(false);
        skip_->setEnabled(false); later_->setEnabled(false);
        cancel_->setText(tr("Cancel update"));
        cancel_->setVisible(true); cancel_->setEnabled(true);
    }
    QString handoffText;
    switch(handoffState_) {
    case UpdateHandoffState::Idle: break;
    case UpdateHandoffState::Preparing:
        handoffText=tr("Preparing the update. Your session is being saved..."); break;
    case UpdateHandoffState::Waiting:
    case UpdateHandoffState::ExitCommitted:
        handoffText=tr("Closing ZzLogg and starting the update..."); break;
    case UpdateHandoffState::Cancelled:
        handoffText=tr("Update cancelled. Your session is unchanged."); break;
    case UpdateHandoffState::Failed:
        switch(handoffError_) {
        case UpdateHandoffError::Preparation:
            handoffText=tr("Unable to prepare the update. Your session is unchanged."); break;
        case UpdateHandoffError::Blocked:
            handoffText=tr("Another ZzLogg instance is active in this installation. Close it and try again."); break;
        case UpdateHandoffError::Abandoned:
            handoffText=tr("A previous update did not finish cleanly. Wait a moment and try again."); break;
        case UpdateHandoffError::Unavailable:
            handoffText=tr("The installation directory could not be verified. The update was not started."); break;
        case UpdateHandoffError::Commit:
            handoffText=tr("The update could not be committed. Your session is unchanged."); break;
        case UpdateHandoffError::ApprovalDeclined:
            handoffText=tr("Administrator approval was declined. No changes were made."); break;
        case UpdateHandoffError::Closed:
            handoffText=tr("Updates are not available for this installation."); break;
        case UpdateHandoffError::None:
        case UpdateHandoffError::Helper:
            handoffText=tr("The update helper could not be started. Your session is unchanged."); break;
        }
        break;
    }
    handoffStatus_->setText(handoffText); handoffStatus_->setVisible(!handoffText.isEmpty());
}
void UpdateCheckDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if(event->type()==QEvent::LanguageChange) refresh();
}
void UpdateCheckDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if(screen()) resize(size().boundedTo(screen()->availableGeometry().size()-QSize(48,80)));
}
void UpdateCheckDialog::reject()
{
    // Closing or ESC during an active handoff is exactly the cancel operation;
    // the dialog stays open so the cancelled/failed state remains visible.
    if(handoffState_==UpdateHandoffState::Preparing || handoffState_==UpdateHandoffState::Waiting) {
        Q_EMIT installCancelRequested();
        return;
    }
    const bool cancel=snapshot_.status==CheckStatus::Checking && snapshot_.presentToUser;
    const bool cancelDownload=downloadSnapshot_.status==DownloadStatus::Downloading;
    const QPointer<UpdateCheckDialog> guard(this);
    dismissed_=true;
    Q_EMIT closing();
    if(!guard) return;
    // Hide before cancellation can synchronously publish another snapshot.
    QDialog::reject();
    if(guard && cancel) Q_EMIT cancelRequested();
    if(guard && cancelDownload) Q_EMIT downloadCancelRequested();
}
