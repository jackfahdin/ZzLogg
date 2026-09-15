#include "updatesettingspage.h"
#include "configuration.h"
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
UpdateSettingsPage::UpdateSettingsPage(QWidget* parent):QWidget(parent)
{
    setObjectName("updateSettingsPage");
    auto* layout=new QVBoxLayout(this); layout->setSpacing(16);
    automatic_=new QCheckBox(this); automatic_->setObjectName("updateAutomatic");
    channelLabel_=new QLabel(this); channel_=new QComboBox(this); channel_->setObjectName("updateChannel");
    channel_->addItem(QString(),"stable"); channel_->addItem(QString(),"preview");
    channelLabel_->setBuddy(channel_);
    explanation_=new QLabel(this); explanation_->setWordWrap(true);
    applied_=new QLabel(this); applied_->setObjectName("updateAppliedChannel"); applied_->setWordWrap(true);
    check_=new QPushButton(this); check_->setObjectName("updateCheckNow"); check_->setAutoDefault(false);
    layout->addWidget(automatic_); layout->addWidget(channelLabel_); layout->addWidget(channel_);
    layout->addWidget(explanation_); layout->addWidget(applied_); layout->addWidget(check_,0,Qt::AlignLeft);
    layout->addStretch();
    connect(check_,&QPushButton::clicked,this,&UpdateSettingsPage::checkRequested);
    loadFromConfig(); retranslate();
}
void UpdateSettingsPage::loadFromConfig()
{
    const auto& config=Configuration::get();
    automatic_->setChecked(config.versionCheckingEnabled());
    channel_->setCurrentIndex(channel_->findData(config.updateChannel()));
    refreshAppliedChannel();
}
void UpdateSettingsPage::applyToConfig()
{
    auto& config=Configuration::get();
    config.setVersionCheckingEnabled(automatic_->isChecked());
    config.setUpdateChannel(channel_->currentData().toString());
}
void UpdateSettingsPage::refreshAppliedChannel()
{
    applied_->setText(tr("Check now uses the applied channel: %1")
        .arg(Configuration::get().updateChannel()=="preview" ? tr("Preview") : tr("Stable")));
}
void UpdateSettingsPage::retranslate()
{
    automatic_->setText(tr("Automatically check for updates"));
    channelLabel_->setText(tr("Update channel"));
    channel_->setItemText(0,tr("Stable")); channel_->setItemText(1,tr("Preview"));
    explanation_->setText(tr("Stable is recommended. Preview releases may contain unfinished changes. Channel and automatic checking changes take effect after Apply or OK."));
    check_->setText(tr("Check now")); refreshAppliedChannel();
}
void UpdateSettingsPage::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if(event->type()==QEvent::LanguageChange) retranslate();
}
