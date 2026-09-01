#pragma once

#include "storagecontext.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

// Reusable selector that validates a choice without persisting it.
class StorageLocationPage final : public QWidget {
    Q_OBJECT

public:
    explicit StorageLocationPage( QWidget* parent = nullptr );

    void setApplicationDirectory( QString path );
    void setUserDataDirectory( QString path );
    void setLocation( const StorageLocation& location );
    StorageLocation location() const;
    bool isSelectionValid() const;
    QString validationError() const;
    void setCommandLineManaged( bool managed );

Q_SIGNALS:
    void validityChanged( bool valid );

private:
    QString rootForSelection() const;
    void refreshValidation();
    void updateEditControls();
    bool validateProgramLocatorDirectory( QString* error ) const;

    QRadioButton* userStorageRadio_ = nullptr;
    QRadioButton* programStorageRadio_ = nullptr;
    QRadioButton* customStorageRadio_ = nullptr;
    QLineEdit* storagePathPreview_ = nullptr;
    QLineEdit* customStoragePath_ = nullptr;
    QPushButton* browseStorageButton_ = nullptr;
    QPushButton* openStorageDirectoryButton_ = nullptr;
    QLabel* storageValidationLabel_ = nullptr;
    QString applicationDirectory_;
    QString userDataDirectory_;
    StorageMode mode_ = StorageMode::UserDirectory;
    bool commandLineManaged_ = false;
    bool selectionValid_ = false;
    QString validationError_;
};
