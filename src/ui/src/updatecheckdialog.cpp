#include "updatecheckdialog.h"
#include "configuration.h"
#include "klogg_version.h"
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>
using namespace zzlogg::updateqt;
namespace {
QLabel* label(QWidget* parent,const char* objectName=nullptr) {
    auto* result=new QLabel(parent);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if(objectName) result->setObjectName(objectName);
    return result;
}
}
void UpdateCheckDialog::setMuted(QLabel* label,bool muted)
{
    if(!label) return;
    QPalette palette=label->palette();
    palette.setColor(QPalette::WindowText,
        muted ? palette.color(QPalette::PlaceholderText) : palette.color(QPalette::WindowText));
    label->setPalette(palette);
}
UpdateCheckDialog::UpdateCheckDialog(QWidget* parent):QDialog(parent)
{
    setObjectName("updateCheckDialog");
    setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    auto* outer=new QVBoxLayout(this);
    outer->setContentsMargins(24,22,24,20);
    outer->setSpacing(18);

    identity_=label(this,"updateIdentity");
    setMuted(identity_,true);
    status_=label(this,"updateStatus");
    QFont headline=status_->font();
    headline.setPointSize(headline.pointSize()+3);
    headline.setWeight(QFont::DemiBold);
    status_->setFont(headline);
    hint_=label(this,"updateHint");
    setMuted(hint_,true);
    outer->addWidget(identity_);
    outer->addWidget(status_);
    outer->addWidget(hint_);

    notesFrame_=new QFrame(this);
    notesFrame_->setObjectName("updateNotesFrame");
    notesFrame_->setFrameShape(QFrame::StyledPanel);
    auto* notesLayout=new QVBoxLayout(notesFrame_);
    notesLayout->setContentsMargins(12,10,12,10);
    notes_=new QPlainTextEdit(notesFrame_);
    notes_->setReadOnly(true);
    notes_->setObjectName("updateNotes");
    notes_->setFrameShape(QFrame::NoFrame);
    notes_->setMinimumHeight(140);
    notes_->setMaximumHeight(280);
    notesLayout->addWidget(notes_);
    outer->addWidget(notesFrame_);

    details_=label(this,"updateDetails");
    setMuted(details_,true);
    outer->addWidget(details_);

    downloadStatus_=label(this,"updateDownloadStatus");
    setMuted(downloadStatus_,true);
    progress_=new QProgressBar(this);
    progress_->setObjectName("updateDownloadProgress");
    progress_->setTextVisible(false);
    progress_->setFixedHeight(8);
    outer->addWidget(downloadStatus_);
    outer->addWidget(progress_);

    handoffStatus_=label(this,"updateHandoffStatus");
    setMuted(handoffStatus_,true);
    outer->addWidget(handoffStatus_);

    auto* footer=new QHBoxLayout;
    footer->setSpacing(12);
    skip_=new QPushButton(this);
    skip_->setObjectName("updateSkip");
    later_=new QPushButton(this);
    later_->setObjectName("updateLater");
    cancel_=new QPushButton(this);
    cancel_->setObjectName("updateCancel");
    download_=new QPushButton(this);
    download_->setObjectName("updateDownload");
    install_=new QPushButton(this);
    install_->setObjectName("updateInstall");
    for(auto* button:{skip_,later_}) {
        button->setFlat(true);
        button->setAutoDefault(false);
        footer->addWidget(button);
    }
    footer->addStretch();
    for(auto* button:{cancel_,download_,install_}) {
        button->setAutoDefault(false);
        footer->addWidget(button);
    }
    outer->addLayout(footer);

    connect(download_,&QPushButton::clicked,this,&UpdateCheckDialog::downloadRequested);
    connect(install_,&QPushButton::clicked,this,[this] {
        if(installAvailable()) Q_EMIT installRequested();
    });
    connect(cancel_,&QPushButton::clicked,this,[this] {
        if(handoffState_==UpdateHandoffState::Preparing || handoffState_==UpdateHandoffState::Waiting)
            Q_EMIT installCancelRequested();
        else if(downloadSnapshot_.status==DownloadStatus::Downloading)
            Q_EMIT downloadCancelRequested();
        else
            Q_EMIT cancelRequested();
    });
    connect(skip_,&QPushButton::clicked,this,&UpdateCheckDialog::skipRequested);
    connect(later_,&QPushButton::clicked,this,&UpdateCheckDialog::reject);

    resize(520,440);
    refresh();
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
    updateExecutionAvailable_=false;
    refresh();
}
void UpdateCheckDialog::setDownloadSnapshot(const DownloadSnapshot& snapshot)
{
    downloadSnapshot_=snapshot;
    updateExecutionAvailable_=false;
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
    identity_->setText(tr("Current version %1 · %2 channel").arg(kloggVersion(),channel));

    QString text;
    switch(snapshot_.status) {
    case CheckStatus::Idle: text=tr("Ready to check for updates."); break;
    case CheckStatus::NotConfigured: text=tr("Update service is not configured."); break;
    case CheckStatus::Checking: text=tr("Checking for updates..."); break;
    case CheckStatus::Cancelled: text=tr("Update check cancelled."); break;
    case CheckStatus::UpToDate: text=tr("You are using the latest version."); break;
    case CheckStatus::Available: text=tr("A new version is available."); break;
    case CheckStatus::ManualUpdateAvailable: text=tr("A new version is available."); break;
    case CheckStatus::ReleaseInformation: text=tr("Release information is available."); break;
    case CheckStatus::Unsupported: text=tr("This release is not compatible with this installation."); break;
    case CheckStatus::NetworkError: text=tr("Unable to retrieve the update manifest. Please try again later."); break;
    case CheckStatus::VerificationFailed: text=tr("Update verification failed. Check the system clock or try again later."); break;
    case CheckStatus::StateInvalid: text=tr("Local update state cannot be read. It has not been reset."); break;
    case CheckStatus::StateBusy: text=tr("Another process is updating the local state. Please try again later."); break;
    case CheckStatus::StateWriteFailed: text=tr("Unable to save update state. No update has been accepted."); break;
    }
    status_->setText(text);

    QStringList meta;
    if(checkedAt_.isValid())
        meta.append(tr("Last check: %1").arg(checkedAt_.toString(Qt::ISODate)));
    QString notes;
    if(snapshot_.release) {
        const auto& release=snapshot_.release->manifest();
        const auto version=QString("%1.%2.%3").arg(release.version.year,2,10,QChar('0'))
            .arg(release.version.month,2,10,QChar('0')).arg(release.version.patch,2,10,QChar('0'));
        meta.append(tr("Release %1").arg(version));
        const auto language=Configuration::get().language();
        const size_t index=language=="zh_CN" ? 1 : language=="zh_TW" ? 2 : 0;
        notes=QString::fromStdString(release.notes[index]);
        if(snapshot_.decision && snapshot_.decision->artifact) {
            const auto& artifact=*snapshot_.decision->artifact;
            meta.append(tr("%1 · %2 bytes")
                .arg(artifact.distribution==zzlogg::update::Distribution::Portable ? tr("Portable") : tr("Installer"))
                .arg(qulonglong(artifact.size)));
        }
    }
    details_->setText(meta.join(QStringLiteral("  ·  ")));
    details_->setVisible(!meta.isEmpty());
    if(notes_->toPlainText()!=notes) notes_->setPlainText(notes);
    const bool hasNotes=snapshot_.release.has_value() && !notes.trimmed().isEmpty();
    notes_->setVisible(hasNotes);
    notesFrame_->setVisible(hasNotes);

    QString hintText;
    if(snapshot_.status==CheckStatus::NotConfigured)
        hintText=tr("This build cannot use the online update service.");
    else if(snapshot_.status==CheckStatus::ReleaseInformation || snapshot_.status==CheckStatus::Unsupported
        || snapshot_.status==CheckStatus::ManualUpdateAvailable)
        hintText=tr("Automatic installation is unavailable for this installation.");
    else if(installAvailable())
        hintText=tr("The update package is verified. You can install it now.");
    else if(updateExecutionAvailable_)
        hintText=tr("Download and verify the update before installing.");
    else if(snapshot_.status==CheckStatus::Available)
        hintText=tr("Install in place is not available for this build.");
    hint_->setText(hintText);
    hint_->setVisible(!hintText.isEmpty());

    cancel_->setText(tr("Cancel"));
    later_->setText(tr("Remind me later"));
    skip_->setText(tr("Skip this version"));

    const bool checking=snapshot_.status==CheckStatus::Checking;
    const bool downloading=downloadSnapshot_.status==DownloadStatus::Downloading;
    const bool downloadable=snapshot_.status==CheckStatus::Available && snapshot_.release && snapshot_.decision
        && snapshot_.decision->status==zzlogg::update::DecisionStatus::Available && snapshot_.decision->artifact;
    const bool retry=downloadSnapshot_.status==DownloadStatus::Failed || downloadSnapshot_.status==DownloadStatus::Cancelled;
    download_->setText(retry ? tr("Retry download") : tr("Download update"));
    download_->setVisible(downloadable);
    download_->setEnabled(downloadable && !downloading && downloadSnapshot_.status!=DownloadStatus::Verified
        && downloadSnapshot_.status!=DownloadStatus::Unavailable);
    download_->setDefault(download_->isVisible() && download_->isEnabled() && !installAvailable());

    cancel_->setVisible(checking || downloading);
    cancel_->setEnabled(checking || downloading);
    if(downloading) cancel_->setText(tr("Cancel download"));
    else if(checking) cancel_->setText(tr("Cancel check"));

    const bool offerDismiss=snapshot_.release.has_value() && !downloading
        && handoffState_!=UpdateHandoffState::Preparing && handoffState_!=UpdateHandoffState::Waiting;
    skip_->setVisible(offerDismiss);
    later_->setVisible(offerDismiss);
    skip_->setEnabled(offerDismiss && !checking);
    later_->setEnabled(offerDismiss && !checking);

    QString downloadText;
    switch(downloadSnapshot_.status) {
    case DownloadStatus::Idle: break;
    case DownloadStatus::Downloading: downloadText=tr("Downloading update..."); break;
    case DownloadStatus::Verified: downloadText=tr("Download verified."); break;
    case DownloadStatus::Cancelled: downloadText=tr("Download cancelled. You can retry."); break;
    case DownloadStatus::Unavailable: downloadText=tr("This download is no longer available."); break;
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
        progress_->setRange(0,total>0 ? 100 : 0);
        progress_->setValue(percent);
        downloadText+=QStringLiteral("\n")+(total>0
            ? tr("%1 / %2 bytes (%3%)").arg(received).arg(total).arg(percent)
            : tr("%1 bytes received").arg(received));
    }
    downloadStatus_->setText(downloadText);
    downloadStatus_->setVisible(!downloadText.isEmpty());

    const bool handoffBusy=handoffState_==UpdateHandoffState::Preparing
        || handoffState_==UpdateHandoffState::Waiting;
    install_->setText(tr("Quit and install update"));
    install_->setVisible(installAvailable() || handoffBusy);
    install_->setEnabled(installAvailable() && !handoffBusy);
    install_->setDefault(install_->isVisible() && install_->isEnabled());

    if(handoffBusy) {
        download_->setEnabled(false);
        skip_->setEnabled(false);
        later_->setEnabled(false);
        cancel_->setText(tr("Cancel update"));
        cancel_->setVisible(true);
        cancel_->setEnabled(true);
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
    handoffStatus_->setText(handoffText);
    handoffStatus_->setVisible(!handoffText.isEmpty());
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
    QDialog::reject();
    if(guard && cancel) Q_EMIT cancelRequested();
    if(guard && cancelDownload) Q_EMIT downloadCancelRequested();
}
