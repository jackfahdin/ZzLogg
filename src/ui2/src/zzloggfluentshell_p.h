#pragma once

#include <functional>

class QMainWindow;
class QMenuBar;

namespace ZzFluentUI {
class ZzFluentTitleBar;
}

namespace ZzLoggUi2Internal {

using MenuCommitInterruption = std::function<void()>;

void commitFluentMenu( QMainWindow& window, ZzFluentUI::ZzFluentTitleBar& titleBar,
                       QMenuBar* originalMenuBar, MenuCommitInterruption interruption = {} );

} // namespace ZzLoggUi2Internal
