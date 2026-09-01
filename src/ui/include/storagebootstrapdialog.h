#pragma once

#include "storagecontext.h"

#include <QDialog>

#include <optional>

class QPushButton;
class StorageLocationPage;

class StorageBootstrapDialog final : public QDialog {
    Q_OBJECT

public:
    explicit StorageBootstrapDialog( QWidget* parent = nullptr );

    void configurePaths( const QString& applicationDirectory, const QString& userDataDirectory );
    std::optional<StorageLocation> selectedLocation() const;

private:
    StorageLocationPage* storageLocationPage_ = nullptr;
    QPushButton* continueButton_ = nullptr;
};
