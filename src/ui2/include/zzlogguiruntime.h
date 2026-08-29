#pragma once

#include <cstdint>
#include <memory>

#include <QObject>

#include "configuration.h"

class KloggApp;
class MainWindow;

namespace ZzFluentUI {
class ZzThemeController;
enum class ZzThemeMode : std::uint8_t;
} // namespace ZzFluentUI

class ZzLoggUiRuntime final : public QObject {
    Q_OBJECT

public:
    static std::unique_ptr<ZzLoggUiRuntime> create( KloggApp& app, QString* error );
    ~ZzLoggUiRuntime() override;

private:
    friend class RuntimeContractTest;

    explicit ZzLoggUiRuntime( KloggApp& app );
    void decorate( MainWindow& window );
    void applyTheme( UiThemeMode mode, bool persist );

    KloggApp* app_ = nullptr;
    std::unique_ptr<ZzFluentUI::ZzThemeController> theme_;
};
