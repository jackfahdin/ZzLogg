#pragma once

#include <ZzWindowKit/ZzWindowCapability.h>
#include <ZzWindowKit/ZzWindowChromeConfiguration.h>

namespace ZzFluentUI {
class ZzFluentTitleBar;
}

namespace ZzLoggUi2Internal {

[[nodiscard]] ZzWindowKit::ZzWindowChromeConfiguration
buildFluentChromeConfiguration( ZzFluentUI::ZzFluentTitleBar& titleBar,
                                ZzWindowKit::ZzWindowCapabilities capabilities );

} // namespace ZzLoggUi2Internal
