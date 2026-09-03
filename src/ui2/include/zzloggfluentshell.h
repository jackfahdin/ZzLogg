#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QString>

#include <ZzCore/ZzResult.h>
#include <ZzFluentUI/ZzThemeMode.h>

class QEvent;
class QMainWindow;
class QMenuBar;

namespace ZzFluentUI {
class ZzFluentTitleBar;
class ZzThemeController;
} // namespace ZzFluentUI

namespace ZzWindowKit {
class ZzWindowAgent;
}

[[nodiscard]] QString formatZzLoggWindowTitle( const QString& documentName );

class ZzLoggFluentShell final : public QObject {
    Q_OBJECT

public:
    using ChromeConfigurator = std::function<ZzCore::ZzResult<void>(
        QMainWindow&, ZzFluentUI::ZzFluentTitleBar&, ZzWindowKit::ZzWindowAgent& )>;

    static ZzCore::ZzResult<ZzLoggFluentShell*>
    install( QMainWindow& window, ZzFluentUI::ZzThemeController& theme,
             ChromeConfigurator chromeConfigurator = {} );
    ~ZzLoggFluentShell() override;

    void setActiveDocumentName( const QString& documentName );

Q_SIGNALS:
    void themeModeRequested( ZzFluentUI::ZzThemeMode mode );

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    ZzLoggFluentShell( QMainWindow& window, QMenuBar* originalMenuBar,
                       ZzFluentUI::ZzFluentTitleBar* titleBar,
                       std::unique_ptr<ZzWindowKit::ZzWindowAgent> agent,
                       ZzFluentUI::ZzThemeController& theme );

    void syncWindowState();
    void syncTheme();
    void requestThemeToggle();
    void setAlwaysOnTop( bool requested );

    QPointer<QMainWindow> window_;
    QPointer<QMenuBar> originalMenuBar_;
    QPointer<ZzFluentUI::ZzFluentTitleBar> titleBar_;
    std::unique_ptr<ZzWindowKit::ZzWindowAgent> agent_;
    QPointer<ZzFluentUI::ZzThemeController> theme_;
};
