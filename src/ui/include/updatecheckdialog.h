#pragma once
#include <QDialog>
#include <QDateTime>
#include "zzlogg/updateqt/updateservice.h"
#include "zzlogg/updateqt/updatedownloadservice.h"
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QProgressBar;
class UpdateCheckDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateCheckDialog(QWidget* parent=nullptr);
    ~UpdateCheckDialog() override;
    void setSnapshot(const zzlogg::updateqt::CheckSnapshot&);
    void setDownloadSnapshot(const zzlogg::updateqt::DownloadSnapshot&);
Q_SIGNALS:
    void checkRequested();
    void cancelRequested();
    void skipRequested();
    void downloadRequested();
    void downloadCancelRequested();
    void closing();
protected:
    void changeEvent(QEvent*) override;
    void showEvent(QShowEvent*) override;
    void reject() override;
private:
    void refresh();
    bool dismissed_=false;
    zzlogg::updateqt::CheckSnapshot snapshot_;
    zzlogg::updateqt::DownloadSnapshot downloadSnapshot_;
    QDateTime checkedAt_;
    QLabel *identity_, *status_, *details_, *hint_, *downloadStatus_;
    QProgressBar* progress_;
    QPlainTextEdit* notes_;
    QPushButton *check_, *cancel_, *later_, *skip_, *close_, *download_;
};
