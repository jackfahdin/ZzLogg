#include <QtGlobal>
// Test host only: keep the real decoder/package code and omit application log setup.
namespace logging {
bool needLogging(QtMsgType) { return false; }
}
