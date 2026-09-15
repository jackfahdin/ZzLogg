#include "updatecheckdialog.h"
#include "configuration.h"
#include "klogg_version.h"
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
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
    status_->setObjectName("updateStatus"); details_->setObjectName("updateDetails");
    auto font=status_->font(); font.setBold(true); font.setPointSize(font.pointSize()+2); status_->setFont(font);
    notes_=new QPlainTextEdit(content); notes_->setReadOnly(true); notes_->setObjectName("updateNotes");
    notes_->setMinimumHeight(120); notes_->setFrameShape(QFrame::NoFrame);
    body->addWidget(identity_); body->addWidget(status_); body->addWidget(details_);
    body->addWidget(notes_,1); body->addWidget(hint_);
    scroll->setWidget(content); outer->addWidget(scroll,1);
    auto* footer=new QHBoxLayout; outer->addLayout(footer);
    check_=new QPushButton(this); cancel_=new QPushButton(this); later_=new QPushButton(this);
    skip_=new QPushButton(this); close_=new QPushButton(this);
    check_->setObjectName("updateCheck"); cancel_->setObjectName("updateCancel"); close_->setObjectName("updateClose");
    skip_->setObjectName("updateSkip"); later_->setObjectName("updateLater");
    for(auto* button:{check_,cancel_,later_,skip_,close_}) { button->setAutoDefault(false); footer->addWidget(button); }
    connect(check_,&QPushButton::clicked,this,&UpdateCheckDialog::checkRequested);
    connect(cancel_,&QPushButton::clicked,this,&UpdateCheckDialog::cancelRequested);
    connect(skip_,&QPushButton::clicked,this,&UpdateCheckDialog::skipRequested);
    connect(later_,&QPushButton::clicked,this,&UpdateCheckDialog::reject);
    connect(close_,&QPushButton::clicked,this,&UpdateCheckDialog::reject);
    resize(600,480); refresh();
}
void UpdateCheckDialog::setSnapshot(const CheckSnapshot& snapshot)
{
    snapshot_=snapshot;
    checkedAt_=snapshot.checkedAt>0 ? QDateTime::fromSecsSinceEpoch(snapshot.checkedAt) : QDateTime{};
    refresh();
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
    details_->setText(details); notes_->setPlainText(notes); notes_->setVisible(snapshot_.release.has_value());
    hint_->setText(tr("This version supports update checks only. Download and installation are not available yet."));
    check_->setText(tr("Check again")); cancel_->setText(tr("Cancel check"));
    later_->setText(tr("Remind me later")); skip_->setText(tr("Skip this version")); close_->setText(tr("Close"));
    const bool checking=snapshot_.status==CheckStatus::Checking;
    check_->setEnabled(!checking); cancel_->setVisible(checking); cancel_->setEnabled(checking);
    skip_->setVisible(snapshot_.release.has_value()); later_->setVisible(snapshot_.release.has_value());
    skip_->setEnabled(!checking); later_->setEnabled(!checking);
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
    const bool cancel=snapshot_.status==CheckStatus::Checking && snapshot_.presentToUser;
    const QPointer<UpdateCheckDialog> guard(this);
    // Hide before cancellation can synchronously publish another snapshot.
    QDialog::reject();
    if(guard && cancel) Q_EMIT cancelRequested();
}
