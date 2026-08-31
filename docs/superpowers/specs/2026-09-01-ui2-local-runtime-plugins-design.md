# UI2 本地运行时插件设计

## 背景

Windows 下直接双击普通构建目录中的 `ZzLogg_ui2.exe` 会立即退出。进程返回码为 1，错误为应用图标资源无法加载。资源已经编入可执行文件，真正缺失的是 Qt SVG 图标引擎；现有构建后步骤只复制了 Qt 运行时 DLL 和 `platforms/qwindows.dll`。

现有 CTest 通过 `QT_PLUGIN_PATH` 指向本机 Qt 安装，因此掩盖了普通构建目录不自包含的问题。绿色包通过 `windeployqt` 已经包含所需插件，不受影响。

## 目标与边界

- Windows 普通构建目录必须包含 `platforms/qwindows`、`iconengines/qsvgicon` 和 `imageformats/qsvg` 插件。
- `ZzLogg_ui2.exe` 在清除 `QT_PLUGIN_PATH` 后仍能从自己的输出目录完成启动冒烟测试。
- Debug 使用带 `d` 后缀的插件，RelWithDebInfo 等非 Debug 配置使用无后缀插件。
- 不改变应用图标格式，不在每次普通编译后运行完整 `windeployqt`，不修改非 Windows 部署行为。

## 方案选择

采用扩展现有 `klogg_copy_ui2_runtime_dlls()` 的方案：继续使用 CMake 的 Qt 导入目标，在构建后把 `Qt6::QSvgIconPlugin` 和 `Qt6::QSvgPlugin` 分别复制到 `iconengines` 与 `imageformats`。

没有采用以下方案：

- 每次构建运行 `windeployqt`：依赖闭包完整，但显著增加日常编译耗时并扩大输出内容。
- 把应用图标改回 PNG：只能绕过当前启动点，无法保证程序中其他 SVG 资源可用。

## 测试设计

新增 Windows 本地运行时合约测试。测试先检查三类插件真实存在于可执行文件相邻目录，再清除 `QT_PLUGIN_PATH`，使用隔离配置目录运行真实 `ZzLogg_ui2.exe` 的定时冒烟模式。

该测试应在旧实现上因缺少 `iconengines/qsvgicon*.dll` 失败，在部署辅助函数补齐两个 SVG 插件后通过。完整 Debug 与 RelWithDebInfo 构建和 CTest 用于验证配置后缀及回归。
