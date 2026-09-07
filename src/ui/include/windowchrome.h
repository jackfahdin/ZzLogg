#pragma once
#include <functional>
#include <memory>
#include <QObject>
#include <QPointer>
#include <ZzCore/ZzResult.h>
#include <ZzFluentUI/ZzThemeMode.h>
#include "uithemecontext.h"
class QMainWindow;
class QMenuBar;
namespace ZzFluentUI { class ZzFluentTitleBar; }
namespace ZzWindowKit { class ZzWindowAgent; }
[[nodiscard]] QString formatWindowTitle(const QString& documentName);

// Owned by MainWindow, not by the QObject child tree. Destroy before window widgets.
class WindowChrome final : public QObject {
    Q_OBJECT
public:
    using ChromeConfigurator = std::function<ZzCore::ZzResult<void>(
        QMainWindow&, ZzFluentUI::ZzFluentTitleBar&, ZzWindowKit::ZzWindowAgent&)>;
    WindowChrome(QMainWindow& window, UiThemeContext context, ChromeConfigurator configure = {});
    ~WindowChrome() override;
    QMenuBar& commandMenuBar() const;
    bool usesNativeFallback() const;
    void setDocumentName(const QString& documentName);
Q_SIGNALS:
    void themeModeRequested(ZzFluentUI::ZzThemeMode mode);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void syncWindowState();
    void syncTheme();
    void requestThemeToggle();
    void setAlwaysOnTop(bool requested);
    QPointer<QMainWindow> window_;
    QPointer<QMenuBar> menu_;
    QPointer<ZzFluentUI::ZzFluentTitleBar> titleBar_;
    std::unique_ptr<ZzWindowKit::ZzWindowAgent> agent_;
    QPointer<ZzFluentUI::ZzThemeController> theme_;
};
