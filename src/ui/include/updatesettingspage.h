#pragma once
#include <QWidget>
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class UpdateSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit UpdateSettingsPage(QWidget* parent=nullptr);
    void loadFromConfig();
    void applyToConfig();
    void refreshAppliedChannel();
Q_SIGNALS:
    void checkRequested();
protected:
    void changeEvent(QEvent*) override;
private:
    void retranslate();
    QCheckBox* automatic_;
    QComboBox* channel_;
    QLabel *channelLabel_, *explanation_, *applied_;
    QPushButton* check_;
};
