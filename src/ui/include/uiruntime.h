#pragma once
#include <memory>
#include <QObject>
#include "configuration.h"
#include "session.h"
class KloggApp;
class MainWindow;
namespace ZzFluentUI { class ZzThemeController; }
class UiRuntime final : public QObject {
    Q_OBJECT
public:
    static std::unique_ptr<UiRuntime> create(KloggApp& app, QString* error);
    ~UiRuntime() override;
private:
    explicit UiRuntime(KloggApp& app);
    MainWindow* createWindow(WindowSession session);
    void applyTheme(UiThemeMode mode, bool persist);
    KloggApp* app_ = nullptr;
    std::unique_ptr<ZzFluentUI::ZzThemeController> theme_;
};
