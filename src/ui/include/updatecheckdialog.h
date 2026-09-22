#pragma once
#include <QDialog>
#include <QDateTime>
#include "zzlogg/updateqt/updateservice.h"
#include "zzlogg/updateqt/updatedownloadservice.h"
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QProgressBar;
class QFrame;
class UpdateCheckDialog : public QDialog {
    Q_OBJECT
public:
    enum class UpdateHandoffState { Idle, Preparing, Waiting, ExitCommitted, Cancelled, Failed };
    enum class UpdateHandoffError { None, Preparation, Blocked, Abandoned, Unavailable, Helper, Commit,
        ApprovalDeclined, Closed };
    explicit UpdateCheckDialog(QWidget* parent=nullptr);
    ~UpdateCheckDialog() override;
    void setSnapshot(const zzlogg::updateqt::CheckSnapshot&);
    void setDownloadSnapshot(const zzlogg::updateqt::DownloadSnapshot&);
    void setUpdateExecutionAvailable(bool available);
    void setHandoffState(UpdateHandoffState state,
                         UpdateHandoffError error=UpdateHandoffError::None);
Q_SIGNALS:
    void checkRequested();
    void cancelRequested();
    void skipRequested();
    void downloadRequested();
    void downloadCancelRequested();
    void installRequested();
    void installCancelRequested();
    void closing();
protected:
    void changeEvent(QEvent*) override;
    void showEvent(QShowEvent*) override;
    void reject() override;
private:
    void refresh();
    bool installAvailable() const;
    static void setMuted(QLabel* label,bool muted);
    bool dismissed_=false;
    bool updateExecutionAvailable_=false;
    UpdateHandoffState handoffState_=UpdateHandoffState::Idle;
    UpdateHandoffError handoffError_=UpdateHandoffError::None;
    zzlogg::updateqt::CheckSnapshot snapshot_;
    zzlogg::updateqt::DownloadSnapshot downloadSnapshot_;
    QDateTime checkedAt_;
    QLabel *identity_, *status_, *details_, *hint_, *downloadStatus_, *handoffStatus_;
    QProgressBar* progress_;
    QPlainTextEdit* notes_;
    QFrame* notesFrame_;
    QPushButton *cancel_, *later_, *skip_, *download_, *install_;
};
