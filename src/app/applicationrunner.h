#pragma once

#include <functional>
#include <memory>

#include <QString>

class KloggApp;
class QObject;

inline constexpr int ZzLoggRestartExitCode = 773;

using KloggUiRuntimeFactory = std::function<std::unique_ptr<QObject>( KloggApp&, QString* error )>;

struct KloggApplicationOptions final {
    KloggUiRuntimeFactory createUiRuntime;
    QString startupWarning;
};

int runKloggApplication( int argc, char* argv[], KloggApplicationOptions options = {} );
