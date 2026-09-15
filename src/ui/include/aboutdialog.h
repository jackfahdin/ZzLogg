#pragma once

#include <QDialog>

class QLabel;
class QPushButton;

class AboutDialog final : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    QLabel* description_;
    QLabel* maintainerTitle_;
    QLabel* buildTitle_;
    QLabel* commitTitle_;
    QLabel* homepage_;
    QLabel* creditsTitle_;
    QLabel* credits_;
    QPushButton* close_;
};
