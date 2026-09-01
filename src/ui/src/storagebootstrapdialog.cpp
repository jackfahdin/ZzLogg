#include "storagebootstrapdialog.h"

#include "storagelocationpage.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

StorageBootstrapDialog::StorageBootstrapDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "选择 ZzLogg 数据保存位置" ) );

    auto* layout = new QVBoxLayout{ this };
    storageLocationPage_ = new StorageLocationPage{ this };
    layout->addWidget( storageLocationPage_ );

    auto* buttons = new QDialogButtonBox{ this };
    buttons->setObjectName( QStringLiteral( "storageDialogButtons" ) );
    continueButton_ = buttons->addButton( tr( "继续" ), QDialogButtonBox::AcceptRole );
    continueButton_->setObjectName( QStringLiteral( "storageContinueButton" ) );
    auto* cancelButton = buttons->addButton( tr( "取消" ), QDialogButtonBox::RejectRole );
    cancelButton->setObjectName( QStringLiteral( "storageCancelButton" ) );
    layout->addWidget( buttons );

    continueButton_->setEnabled( storageLocationPage_->isSelectionValid() );
    connect( storageLocationPage_, &StorageLocationPage::validityChanged, continueButton_,
             &QPushButton::setEnabled );
    connect( continueButton_, &QPushButton::clicked, this, [ this ] {
        if ( storageLocationPage_->isSelectionValid() ) {
            accept();
        }
    } );
    connect( cancelButton, &QPushButton::clicked, this, &QDialog::reject );
}

void StorageBootstrapDialog::configurePaths( const QString& applicationDirectory,
                                             const QString& userDataDirectory )
{
    storageLocationPage_->setApplicationDirectory( applicationDirectory );
    storageLocationPage_->setUserDataDirectory( userDataDirectory );
    continueButton_->setEnabled( storageLocationPage_->isSelectionValid() );
}

std::optional<StorageLocation> StorageBootstrapDialog::selectedLocation() const
{
    if ( result() != QDialog::Accepted || !storageLocationPage_->isSelectionValid() ) {
        return std::nullopt;
    }
    return storageLocationPage_->location();
}
