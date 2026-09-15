#pragma once
#include <QDialog>
#include <QDateTime>
#include "zzlogg/updateqt/updateservice.h"
class QLabel;
class QPushButton;
class QPlainTextEdit;
class UpdateCheckDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateCheckDialog(QWidget* parent=nullptr);
    void setSnapshot(const zzlogg::updateqt::CheckSnapshot&);
Q_SIGNALS:
    void checkRequested();
    void cancelRequested();
    void skipRequested();
protected:
    void changeEvent(QEvent*) override;
    void showEvent(QShowEvent*) override;
    void reject() override;
private:
    void refresh();
    zzlogg::updateqt::CheckSnapshot snapshot_;
    QDateTime checkedAt_;
    QLabel *identity_, *status_, *details_, *hint_;
    QPlainTextEdit* notes_;
    QPushButton *check_, *cancel_, *later_, *skip_, *close_;
};
