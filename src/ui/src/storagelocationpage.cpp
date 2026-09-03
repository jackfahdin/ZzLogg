#include "storagelocationpage.h"

#include "storagevalidator.h"

#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace {

QString normalizedPath( const QString& path )
{
    return QDir::cleanPath( QDir::fromNativeSeparators( path ) );
}

} // namespace

StorageLocationPage::StorageLocationPage( QWidget* parent )
    : QWidget( parent )
{
    setObjectName( QStringLiteral( "storageLocationPage" ) );

    auto* layout = new QVBoxLayout{ this };
    explanationLabel_ = new QLabel{ this };
    explanationLabel_->setObjectName( QStringLiteral( "storageLocationExplanation" ) );
    explanationLabel_->setWordWrap( true );
    layout->addWidget( explanationLabel_ );

    userStorageRadio_ = new QRadioButton{ tr( "用户数据目录" ), this };
    userStorageRadio_->setObjectName( QStringLiteral( "userStorageRadio" ) );
    userStorageRadio_->setChecked( true );
    layout->addWidget( userStorageRadio_ );

    programStorageRadio_ = new QRadioButton{ tr( "程序目录（data）" ), this };
    programStorageRadio_->setObjectName( QStringLiteral( "programStorageRadio" ) );
    layout->addWidget( programStorageRadio_ );

    customStorageRadio_ = new QRadioButton{ tr( "自定义目录" ), this };
    customStorageRadio_->setObjectName( QStringLiteral( "customStorageRadio" ) );
    layout->addWidget( customStorageRadio_ );

    auto* customLayout = new QHBoxLayout;
    customLayout->setContentsMargins( 24, 0, 0, 0 );
    customStoragePath_ = new QLineEdit{ this };
    customStoragePath_->setObjectName( QStringLiteral( "customStoragePath" ) );
    customStoragePath_->setPlaceholderText( tr( "请输入绝对路径" ) );
    browseStorageButton_ = new QPushButton{ tr( "浏览…" ), this };
    browseStorageButton_->setObjectName( QStringLiteral( "browseStorageButton" ) );
    customLayout->addWidget( customStoragePath_ );
    customLayout->addWidget( browseStorageButton_ );
    layout->addLayout( customLayout );

    auto* previewLayout = new QFormLayout;
    storagePathPreview_ = new QLineEdit{ this };
    storagePathPreview_->setObjectName( QStringLiteral( "storagePathPreview" ) );
    storagePathPreview_->setReadOnly( true );
    openStorageDirectoryButton_ = new QPushButton{ tr( "打开目录" ), this };
    openStorageDirectoryButton_->setObjectName( QStringLiteral( "openStorageDirectoryButton" ) );
    auto* previewRow = new QHBoxLayout;
    previewRow->addWidget( storagePathPreview_ );
    previewRow->addWidget( openStorageDirectoryButton_ );
    dataDirectoryLabel_ = new QLabel{ this };
    dataDirectoryLabel_->setObjectName( QStringLiteral( "dataDirectoryLabel" ) );
    previewLayout->addRow( dataDirectoryLabel_, previewRow );
    layout->addLayout( previewLayout );

    storageValidationLabel_ = new QLabel{ this };
    storageValidationLabel_->setObjectName( QStringLiteral( "storageValidationLabel" ) );
    storageValidationLabel_->setWordWrap( true );
    layout->addWidget( storageValidationLabel_ );
    layout->addStretch();

    connect( userStorageRadio_, &QRadioButton::toggled, this, [ this ]( bool checked ) {
        if ( checked ) {
            mode_ = StorageMode::UserDirectory;
            updateEditControls();
            refreshValidation();
        }
    } );
    connect( programStorageRadio_, &QRadioButton::toggled, this, [ this ]( bool checked ) {
        if ( checked ) {
            mode_ = StorageMode::ProgramDirectory;
            updateEditControls();
            refreshValidation();
        }
    } );
    connect( customStorageRadio_, &QRadioButton::toggled, this, [ this ]( bool checked ) {
        if ( checked ) {
            mode_ = StorageMode::CustomDirectory;
            updateEditControls();
            refreshValidation();
        }
    } );
    connect( customStoragePath_, &QLineEdit::textChanged, this, [ this ] {
        if ( mode_ == StorageMode::CustomDirectory ) {
            refreshValidation();
        }
    } );
    connect( browseStorageButton_, &QPushButton::clicked, this, [ this ] {
        const QString directory = QFileDialog::getExistingDirectory(
            this, tr( "选择 ZzLogg 数据目录" ), customStoragePath_->text() );
        if ( !directory.isEmpty() ) {
            customStoragePath_->setText( normalizedPath( directory ) );
        }
    } );
    connect( openStorageDirectoryButton_, &QPushButton::clicked, this, [ this ] {
        const QString root = rootForSelection();
        if ( !root.isEmpty() ) {
            QDesktopServices::openUrl( QUrl::fromLocalFile( root ) );
        }
    } );

    updateEditControls();
    retranslateUi();
}

void StorageLocationPage::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::LanguageChange ) {
        retranslateUi();
    }
    QWidget::changeEvent( event );
}

void StorageLocationPage::retranslateUi()
{
    explanationLabel_->setText( tr( "请选择 ZzLogg 数据的保存位置。" ) );
    userStorageRadio_->setText( tr( "用户数据目录" ) );
    programStorageRadio_->setText( tr( "程序目录（data）" ) );
    customStorageRadio_->setText( tr( "自定义目录" ) );
    customStoragePath_->setPlaceholderText( tr( "请输入绝对路径" ) );
    dataDirectoryLabel_->setText( tr( "数据目录：" ) );
    browseStorageButton_->setText( tr( "浏览…" ) );
    openStorageDirectoryButton_->setText( tr( "打开目录" ) );
    refreshValidation( false, false );
}

void StorageLocationPage::setApplicationDirectory( QString path )
{
    applicationDirectory_ = normalizedPath( path );
    refreshValidation();
}

void StorageLocationPage::setUserDataDirectory( QString path )
{
    userDataDirectory_ = normalizedPath( path );
    refreshValidation();
}

void StorageLocationPage::setLocation( const StorageLocation& location )
{
    mode_ = location.mode;
    customStoragePath_->setText( location.mode == StorageMode::CustomDirectory
                                     ? normalizedPath( location.dataRoot )
                                     : customStoragePath_->text() );
    userStorageRadio_->setChecked( mode_ == StorageMode::UserDirectory );
    programStorageRadio_->setChecked( mode_ == StorageMode::ProgramDirectory );
    customStorageRadio_->setChecked( mode_ == StorageMode::CustomDirectory );
    commandLineManaged_ = location.commandLineOverride;
    updateEditControls();
    refreshValidation();
}

StorageLocation StorageLocationPage::location() const
{
    return { mode_, rootForSelection(), {}, commandLineManaged_ };
}

bool StorageLocationPage::isSelectionValid() const
{
    return selectionValid_;
}

QString StorageLocationPage::validationError() const
{
    return validationError_;
}

void StorageLocationPage::setCommandLineManaged( bool managed )
{
    commandLineManaged_ = managed;
    updateEditControls();
    refreshValidation();
}

QString StorageLocationPage::rootForSelection() const
{
    switch ( mode_ ) {
    case StorageMode::UserDirectory:
        return normalizedPath( userDataDirectory_ );
    case StorageMode::ProgramDirectory:
        return applicationDirectory_.isEmpty()
                   ? QString{}
                   : normalizedPath(
                         QDir{ applicationDirectory_ }.filePath( QStringLiteral( "data" ) ) );
    case StorageMode::CustomDirectory:
        return normalizedPath( customStoragePath_->text() );
    }
    return {};
}

void StorageLocationPage::refreshValidation( bool normalizeCustomPath, bool validateStorage )
{
    if ( !validateStorage ) {
        if ( programLocatorValidationError_ != ProgramLocatorValidationError::None ) {
            validationError_ = programLocatorValidationErrorText();
        }
        else {
            validationError_ = storageValidationErrorText();
        }
        storageValidationLabel_->setText( validationError_ );
        storageValidationLabel_->setStyleSheet( validationError_.isEmpty()
                                                    ? QString{}
                                                    : QStringLiteral( "color: #b00020;" ) );
        return;
    }

    const QString root = rootForSelection();
    storagePathPreview_->setText( root );
    openStorageDirectoryButton_->setEnabled( !root.isEmpty() );

    const auto result = StorageValidator::validate( root, true );
    bool valid = result.valid;
    storageValidationError_ = result.errorCode;
    storageValidationErrorParameters_ = result.errorParameters;
    QString error = storageValidationErrorText();
    programLocatorValidationError_ = ProgramLocatorValidationError::None;
    programLocatorProbePath_.clear();
    if ( valid && mode_ == StorageMode::ProgramDirectory
         && !validateProgramLocatorDirectory( &error ) ) {
        valid = false;
    }
    if ( normalizeCustomPath && valid && mode_ == StorageMode::CustomDirectory ) {
        customStoragePath_->setText( result.normalizedRoot );
        storagePathPreview_->setText( result.normalizedRoot );
    }

    validationError_ = error;
    storageValidationLabel_->setText( error );
    storageValidationLabel_->setStyleSheet( error.isEmpty() ? QString{}
                                                            : QStringLiteral( "color: #b00020;" ) );
    if ( selectionValid_ != valid ) {
        selectionValid_ = valid;
        Q_EMIT validityChanged( valid );
    }
}

QString StorageLocationPage::programLocatorValidationErrorText() const
{
    switch ( programLocatorValidationError_ ) {
    case ProgramLocatorValidationError::None:
        return {};
    case ProgramLocatorValidationError::CannotCreateDirectory:
        return tr( "无法创建程序目录以验证存储位置：%1" ).arg( applicationDirectory_ );
    case ProgramLocatorValidationError::CannotRemoveProbe:
        return tr( "无法清理存储定位文件写入探针：%1" ).arg( programLocatorProbePath_ );
    case ProgramLocatorValidationError::CannotWriteProbe:
        return tr( "无法在程序目录旁原子写入存储定位文件：%1" ).arg( applicationDirectory_ );
    case ProgramLocatorValidationError::CannotReadProbe:
        return tr( "写入后无法完整读回存储定位文件探针：%1" )
            .arg( programLocatorProbePath_ );
    }
    return {};
}

QString StorageLocationPage::storageValidationErrorText() const
{
    const QString parameter = storageValidationErrorParameters_.value( 0 );
    switch ( storageValidationError_ ) {
    case StorageValidationError::None:
        return {};
    case StorageValidationError::InvalidRoot:
        return tr( "storage directory must be an absolute, non-empty path" );
    case StorageValidationError::NotDirectory:
        return tr( "storage path is not a directory: %1" ).arg( parameter );
    case StorageValidationError::CannotCreateDirectory:
        return tr( "failed to create storage directory: %1" ).arg( parameter );
    case StorageValidationError::CannotWriteDirectory:
        return tr( "failed to write storage directory: %1" ).arg( parameter );
    case StorageValidationError::CannotReadDirectory:
        return tr( "failed to read storage directory: %1" ).arg( parameter );
    case StorageValidationError::CannotAtomicallyWriteDirectory:
        return tr( "failed to atomically write storage directory: %1" ).arg( parameter );
    case StorageValidationError::CannotReadAtomicWrite:
        return tr( "failed to read atomic storage write: %1" ).arg( parameter );
    case StorageValidationError::CannotRemoveProbe:
        return tr( "failed to remove storage probe file: %1" ).arg( parameter );
    case StorageValidationError::NotEmptyManagedDirectory:
        return tr( "storage directory is not an empty managed directory: %1" ).arg( parameter );
    }
    return {};
}

void StorageLocationPage::updateEditControls()
{
    const bool editable = !commandLineManaged_;
    userStorageRadio_->setEnabled( editable );
    programStorageRadio_->setEnabled( editable );
    customStorageRadio_->setEnabled( editable );
    const bool customEditable = editable && mode_ == StorageMode::CustomDirectory;
    customStoragePath_->setEnabled( customEditable );
    browseStorageButton_->setEnabled( customEditable );
}

bool StorageLocationPage::validateProgramLocatorDirectory( QString* error )
{
    const QFileInfo applicationDirectoryInfo{ applicationDirectory_ };
    if ( applicationDirectory_.isEmpty() || !applicationDirectoryInfo.isAbsolute()
         || !QDir{}.mkpath( applicationDirectory_ ) ) {
        programLocatorValidationError_ = ProgramLocatorValidationError::CannotCreateDirectory;
        *error = programLocatorValidationErrorText();
        return false;
    }

    programLocatorProbePath_ = QDir{ applicationDirectory_ }.filePath(
        QStringLiteral( ".zzlogg-locator-probe-%1" )
            .arg( QUuid::createUuid().toString( QUuid::WithoutBraces ) ) );
    const QByteArray probeBytes{ "zzlogg-locator-atomic-write-check" };
    bool probeCommitted = false;
    bool probeWrittenAndReadable = false;
    {
        QSaveFile probe{ programLocatorProbePath_ };
        if ( probe.open( QIODevice::WriteOnly ) && probe.write( probeBytes ) == probeBytes.size()
             && probe.commit() ) {
            probeCommitted = true;
            QFile readback{ programLocatorProbePath_ };
            if ( readback.open( QIODevice::ReadOnly ) ) {
                const QByteArray readBytes = readback.readAll();
                probeWrittenAndReadable
                    = readback.error() == QFileDevice::NoError && readBytes == probeBytes;
            }
        }
        else {
            probe.cancelWriting();
        }
    }

    const bool finalProbeRemoved = !QFile::exists( programLocatorProbePath_ )
                                  || QFile::remove( programLocatorProbePath_ );
    const QStringList remainingProbeFiles = QDir{ applicationDirectory_ }.entryList(
        { QFileInfo{ programLocatorProbePath_ }.fileName() + QStringLiteral( "*" ) },
        QDir::Files | QDir::Hidden | QDir::System );
    if ( !finalProbeRemoved || !remainingProbeFiles.isEmpty() ) {
        programLocatorValidationError_ = ProgramLocatorValidationError::CannotRemoveProbe;
        *error = programLocatorValidationErrorText();
        return false;
    }
    if ( !probeCommitted ) {
        programLocatorValidationError_ = ProgramLocatorValidationError::CannotWriteProbe;
        *error = programLocatorValidationErrorText();
        return false;
    }
    if ( !probeWrittenAndReadable ) {
        programLocatorValidationError_ = ProgramLocatorValidationError::CannotReadProbe;
        *error = programLocatorValidationErrorText();
        return false;
    }
    return true;
}
