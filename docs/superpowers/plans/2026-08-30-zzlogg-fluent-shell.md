# ZzLogg Fluent 窗口外壳实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 新增独立的 `zzlogg_ui2` 可执行程序，在完全保留现有日志工作区、控件位置和行为的前提下，把菜单栏合入 ZzPureTools Fluent 标题栏，并提供 System / Light / Dark 全局主题。

**架构：** 现有 `KloggApp` 通过可选窗口装饰回调把每个 `MainWindow` 交给 `ZzLoggUiRuntime`；Runtime 统一管理主题，逐窗口安装只负责 chrome 的 `ZzLoggFluentShell`。`main_ui2.cpp` 在 `QApplication` 创建前执行 WindowKit Bootstrap；任何 Bootstrap、attach 或 chrome 配置错误都恢复原生标题栏和原菜单栏，旧 `klogg` / `klogg_portable` 不链接 ZzPureTools。

**技术栈：** CMake 3.25 Presets、C++17（现有代码）、C++20（UI2 边界）、Qt 6.8+ Widgets/Test/Svg、ZzPureTools `f8f60baeec8eed7daae93ff6008a0b93879cfe33`、Qt Test、CTest、MSVC 18 / Ninja。

---

## 实现边界与文件结构

第一阶段只增加普通 `zzlogg_ui2`，不增加 UI2 portable/zip/installer，不改变 `klogg`、`klogg_portable`、`klogg_grep` 的默认构建关系，不替换中心工作区内的任何 Klogg 控件。

**依赖和构建：**

- 创建 `.gitmodules` 和 gitlink `3rdparty/vendor/ZzPureTools`：固定框架来源与提交。
- 创建 `cmake/ZzPureTools.cmake`：在函数作用域隔离 ZzPureTools 的 C++20、共享库和构建选项。
- 修改 `CMakeLists.txt`、`CMakePresets.json`、`CMakeUserPresets.json.example`：增加 opt-in UI2 配置、构建和测试入口。
- 修改 `src/CMakeLists.txt`、创建 `src/ui2/CMakeLists.txt`：只在 `KLOGG_BUILD_UI2=ON` 时进入 UI2 子目录。
- 创建 `tests/ui2/CMakeLists.txt`：UI2 测试不依赖当前缺失的 `backward-cpp`，也不受 `KLOGG_BUILD_TESTS` 控制。

**配置和现有 UI 接缝：**

- 修改 `src/settings/include/configuration.h`、`src/settings/src/configuration.cpp`：定义并持久化 `UiThemeMode`。
- 修改 `src/ui/include/optionsdialog.ui`、`src/ui/src/optionsdialog.cpp`：Fluent 入口显示 Theme，旧入口显示原 Style。
- 修改 `src/ui/include/mainwindow.h`、`src/ui/src/mainwindow.cpp`：发出活动文档名和主题变更的语义信号；不移动工作区控件。
- 修改 `src/ui/src/crawlerwidget.cpp`：只为现有搜索控件补稳定 objectName，供冒烟测试定位。

**应用生命周期：**

- 创建 `src/app/applicationrunner.h`、`src/app/applicationrunner.cpp`：承载两个 GUI 入口共享的原启动流程。
- 修改 `src/app/main.cpp`：变成旧 UI 的薄入口。
- 修改 `src/app/kloggapp.h`：增加可选 `WindowDecorator` 和只读窗口列表。
- 创建 `src/app/main_ui2.cpp`：Bootstrap 后调用共享 runner。
- 修改 `src/app/CMakeLists.txt`：建立 `zzlogg_ui2`，同时保持旧目标源集和链接边界。

**UI2 边界：**

- 创建 `src/ui2/include/zzloggfluentshell.h`、`src/ui2/src/zzloggfluentshell.cpp`：标题、菜单迁移、WindowKit、窗口按钮、置顶和回退事务。
- 创建 `src/ui2/include/zzlogguiruntime.h`、`src/ui2/src/zzlogguiruntime.cpp`：全局主题、Style 生命周期、窗口装饰和错误提示。

**测试与文档：**

- 创建 `tests/ui2/themeconfigurationtest.cpp`：配置格式与非法值归一化。
- 创建 `tests/ui2/optionsthemetest.cpp`：旧 Style / 新 Theme 设置入口互斥。
- 创建 `tests/ui2/fluentshelltest.cpp`、`tests/ui2/windowkittestmain.cpp`：标题、菜单身份、失败回滚、主题、窗口按钮、置顶。
- 创建 `tests/ui2/fixtures/ui2-first.log`、`tests/ui2/fixtures/ui2-second.log`：应用冒烟输入。
- 创建 `tests/ui2/sessionrestoresmoke.cmake`：使用隔离配置目录验证会话恢复后的每个窗口。
- 修改 `docs/BUILD.md`：记录 submodule、Qt 6.8+、跨平台 preset、Windows 验收命令和 UI2 第一阶段边界。

### 任务 1：固定 ZzPureTools 并隔离 UI2 构建开关

**文件：**

- 创建：`.gitmodules`
- 创建：`3rdparty/vendor/ZzPureTools`（gitlink）
- 创建：`cmake/ZzPureTools.cmake`
- 修改：`CMakeLists.txt`

- [ ] **步骤 1：为 worktree 准备被忽略的本机 preset**

把主工作区现有的本机配置复制到当前 worktree；该文件已被 `.gitignore` 排除：

```powershell
Copy-Item -LiteralPath 'D:/File/Program/GitCode/ZzLogg/CMakeUserPresets.json' `
  -Destination 'D:/File/Program/GitCode/ZzLogg-fluent-shell/CMakeUserPresets.json'
```

预期：`cmake --list-presets=all` 能列出 `windows-qt6`，其 Qt 是 `D:/SoftWare/Qt/6.11.0/msvc2022_64`，generator instance 是 `D:/SoftWare/Microsoft Visual Studio/18/Community`；`git status --short` 不显示该文件。

- [ ] **步骤 2：记录当前默认构建基线**

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --preset windows-qt6
& 'D:/SoftWare/CMake/bin/cmake.exe' --build --preset windows-qt6-debug --target klogg klogg_portable
```

预期：默认 preset 不认识任何 UI2 目标，但 `klogg` 与 `klogg_portable` 均可构建；保存完整输出，作为依赖接入后的对照。

- [ ] **步骤 3：添加并固定 submodule**

运行：

```powershell
git submodule add https://github.com/jackfahdin/ZzPureTools.git 3rdparty/vendor/ZzPureTools
git -C 3rdparty/vendor/ZzPureTools checkout --detach f8f60baeec8eed7daae93ff6008a0b93879cfe33
git add .gitmodules 3rdparty/vendor/ZzPureTools
```

预期：`git ls-files --stage 3rdparty/vendor/ZzPureTools` 的 mode 是 `160000`，对象 ID 是 `f8f60baeec8eed7daae93ff6008a0b93879cfe33`。

- [ ] **步骤 4：先写 UI2 配置契约并验证它尚未成立**

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' -S . -B out/build/ui2-contract `
  -G 'Visual Studio 18 2026' -A x64 `
  -DCMAKE_GENERATOR_INSTANCE='D:/SoftWare/Microsoft Visual Studio/18/Community' `
  -DCMAKE_PREFIX_PATH='D:/SoftWare/Qt/6.11.0/msvc2022_64' `
  -DKLOGG_BUILD_TESTS=OFF -DKLOGG_BUILD_UI2=ON -DKLOGG_USE_HYPERSCAN=OFF
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target ZzFluentUI
```

预期：FAIL，`KLOGG_BUILD_UI2` 未被项目消费，且不存在 `ZzFluentUI` 目标。

- [ ] **步骤 5：实现作用域隔离的依赖加载函数**

在 `cmake/ZzPureTools.cmake` 写入以下完整契约：

```cmake
function(klogg_add_zzpuretools)
  set(zz_source_dir "${PROJECT_SOURCE_DIR}/3rdparty/vendor/ZzPureTools")
  if(NOT EXISTS "${zz_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR
      "KLOGG_BUILD_UI2 requires ZzPureTools. Run: git submodule update --init --recursive")
  endif()
  if(CMAKE_VERSION VERSION_LESS 3.23)
    message(FATAL_ERROR "KLOGG_BUILD_UI2 requires CMake 3.23 or newer")
  endif()
  if(Qt6Core_VERSION VERSION_LESS 6.8)
    message(FATAL_ERROR "KLOGG_BUILD_UI2 requires Qt 6.8 or newer")
  endif()

  set(BUILD_SHARED_LIBS ON)
  set(BUILD_TESTING OFF)
  set(ZZ_BUILD_TESTS OFF)
  set(ZZ_BUILD_EXAMPLES OFF)
  set(ZZ_BUILD_BENCHMARKS OFF)
  set(ZZ_ENABLE_ASAN OFF)
  set(ZZ_ENABLE_UBSAN OFF)
  set(ZZ_ENABLE_CLANG_TIDY OFF)
  set(ZZ_ENABLE_MSVC_ANALYZE OFF)
  set(ZZ_WARNINGS_AS_ERRORS OFF)
  set(ZZ_ENABLE_LTO OFF)
  set(ZZ_BUILD_FLUENT_QUICK OFF)
  set(ZZ_RELEASE_BUILD OFF)
  add_subdirectory("${zz_source_dir}"
                   "${CMAKE_BINARY_DIR}/_deps/zzpuretools"
                   EXCLUDE_FROM_ALL)
endfunction()

function(klogg_copy_ui2_runtime_dlls target)
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_RUNTIME_DLLS:${target}>
              $<TARGET_FILE_DIR:${target}>
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()
```

在根 `CMakeLists.txt` 的项目选项区加入：

```cmake
option(KLOGG_BUILD_UI2 "Build the experimental ZzPureTools UI" OFF)
option(KLOGG_BUILD_UI2_TESTS "Build tests for the experimental UI" ${KLOGG_BUILD_UI2})
if(KLOGG_BUILD_UI2_TESTS AND NOT KLOGG_BUILD_UI2)
  message(FATAL_ERROR "KLOGG_BUILD_UI2_TESTS requires KLOGG_BUILD_UI2=ON")
endif()
```

在根文件现有 `find_package(Qt6 COMPONENTS Core Widgets Concurrent Network Xml REQUIRED)` 和全局 `BUILD_SHARED_LIBS OFF` 完成后加入：

```cmake
if(KLOGG_BUILD_UI2)
  include(ZzPureTools)
  klogg_add_zzpuretools()
endif()
```

函数作用域保证 ZzPureTools 看到 `BUILD_SHARED_LIBS=ON` 和 C++20，函数返回后 ZzLogg 仍是 `BUILD_SHARED_LIBS=OFF`、C++17。

- [ ] **步骤 6：重新运行配置契约**

重复步骤 4 的配置命令，然后从 ZzPureTools 的嵌套生成目录构建目标：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' -S . -B out/build/ui2-contract `
  -G 'Visual Studio 18 2026' -A x64 `
  -DCMAKE_GENERATOR_INSTANCE='D:/SoftWare/Microsoft Visual Studio/18/Community' `
  -DCMAKE_PREFIX_PATH='D:/SoftWare/Qt/6.11.0/msvc2022_64' `
  -DKLOGG_BUILD_TESTS=OFF -DKLOGG_BUILD_UI2=ON -DKLOGG_USE_HYPERSCAN=OFF
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract/_deps/zzpuretools/ZzFluentUI `
  --config Debug --target ZzFluentUI
```

预期：PASS；生成并构建 `ZzFluentUI`、`ZzFluentFoundation`、`ZzWindowKit`，根项目的 `CMAKE_CXX_STANDARD` 仍为 `17`。

保留 `EXCLUDE_FROM_ALL` 是依赖隔离契约的一部分。在 Visual Studio 18 / CMake 4.4 生成的根 `klogg.slnx` 中，这会使直接依赖目标不作为根解决方案的可直接构建项目暴露，因此根目录的 `--target ZzFluentUI` 不能作为 GREEN 验收命令。后续根目录构建 `zzlogg_ui2` 时，链接依赖会传递地构建所需的 Zz 库。

- [ ] **步骤 7：确认默认构建没有新增依赖要求**

临时把 submodule 工作树改名后执行默认配置，再恢复原名：

```powershell
Rename-Item -LiteralPath 3rdparty/vendor/ZzPureTools -NewName ZzPureTools.disabled
& 'D:/SoftWare/CMake/bin/cmake.exe' -S . -B out/build/no-ui2-contract `
  -G 'Visual Studio 18 2026' -A x64 `
  -DCMAKE_GENERATOR_INSTANCE='D:/SoftWare/Microsoft Visual Studio/18/Community' `
  -DCMAKE_PREFIX_PATH='D:/SoftWare/Qt/6.11.0/msvc2022_64' `
  -DKLOGG_BUILD_TESTS=OFF -DKLOGG_BUILD_UI2=OFF -DKLOGG_USE_HYPERSCAN=OFF
Rename-Item -LiteralPath 3rdparty/vendor/ZzPureTools.disabled -NewName ZzPureTools
```

预期：PASS；`KLOGG_BUILD_UI2=OFF` 时不读取 submodule。若配置命令失败，也必须先恢复目录名再分析失败。

- [ ] **步骤 8：提交依赖边界**

```powershell
git add .gitmodules 3rdparty/vendor/ZzPureTools cmake/ZzPureTools.cmake CMakeLists.txt
git commit -m "build: 接入可选 ZzPureTools UI2 依赖"
```

### 任务 2：增加可持久化的三态主题配置

**文件：**

- 修改：`src/settings/include/configuration.h`
- 修改：`src/settings/src/configuration.cpp`
- 创建：`tests/ui2/CMakeLists.txt`
- 创建：`tests/ui2/themeconfigurationtest.cpp`
- 修改：`CMakeLists.txt`

- [ ] **步骤 1：建立独立 UI2 测试目录**

根 `CMakeLists.txt` 在现有测试分支之外加入：

```cmake
if(KLOGG_BUILD_UI2_TESTS)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
  add_subdirectory(tests/ui2)
endif()
```

`tests/ui2/CMakeLists.txt` 先建立配置测试：

```cmake
add_executable(zzlogg_theme_configuration_test themeconfigurationtest.cpp)
target_link_libraries(zzlogg_theme_configuration_test
  PRIVATE klogg_settings Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_theme_configuration_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.theme_configuration
         COMMAND zzlogg_theme_configuration_test -platform offscreen)
```

- [ ] **步骤 2：编写失败的配置测试**

在 `themeconfigurationtest.cpp` 使用临时 INI，覆盖规范值、往返保存和非法值立即写回：

```cpp
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include "configuration.h"

class ThemeConfigurationTest final : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void readsAndWritesStableValues()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QStringLiteral("config.ini"));
        QSettings settings(path, QSettings::IniFormat);

        Configuration written;
        written.setUiThemeMode(UiThemeMode::Dark);
        written.saveToStorage(settings);
        settings.sync();
        QCOMPARE(settings.value(QStringLiteral("view.themeMode")).toString(),
                 QStringLiteral("dark"));

        Configuration loaded;
        loaded.retrieveFromStorage(settings);
        QCOMPARE(loaded.uiThemeMode(), UiThemeMode::Dark);
    }

    void normalizesInvalidValueImmediately()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("config.ini")),
                           QSettings::IniFormat);
        settings.setValue(QStringLiteral("view.themeMode"),
                          QStringLiteral("sepia"));
        Configuration loaded;
        loaded.retrieveFromStorage(settings);
        QCOMPARE(loaded.uiThemeMode(), UiThemeMode::System);
        QCOMPARE(settings.value(QStringLiteral("view.themeMode")).toString(),
                 QStringLiteral("system"));
    }
};
QTEST_MAIN(ThemeConfigurationTest)
#include "themeconfigurationtest.moc"
```

- [ ] **步骤 3：运行测试确认失败**

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target zzlogg_theme_configuration_test
```

预期：FAIL，编译器报告 `UiThemeMode`、`setUiThemeMode()` 和 `uiThemeMode()` 未定义。

- [ ] **步骤 4：实现枚举、稳定存储值和非法值归一化**

在 `configuration.h` 定义：

```cpp
#include <QMetaType>
#include <QStringView>

enum class UiThemeMode { System, Light, Dark };
Q_DECLARE_METATYPE(UiThemeMode)

[[nodiscard]] QString uiThemeModeStorageValue(UiThemeMode mode);
[[nodiscard]] UiThemeMode uiThemeModeFromStorageValue(
    QStringView value, bool* valid = nullptr);
```

并在 `Configuration` 中增加：

```cpp
UiThemeMode uiThemeMode() const { return uiThemeMode_; }
void setUiThemeMode(UiThemeMode mode) { uiThemeMode_ = mode; }
// private:
UiThemeMode uiThemeMode_ = UiThemeMode::System;
```

在 `configuration.cpp` 用显式映射实现，不持久化 ZzPureTools 的 `HighContrast`：

```cpp
QString uiThemeModeStorageValue(UiThemeMode mode)
{
    switch (mode) {
    case UiThemeMode::Light: return QStringLiteral("light");
    case UiThemeMode::Dark: return QStringLiteral("dark");
    case UiThemeMode::System: return QStringLiteral("system");
    }
    return QStringLiteral("system");
}

UiThemeMode uiThemeModeFromStorageValue(QStringView value, bool* valid)
{
    const auto normalized = value.trimmed().toString().toLower();
    if (valid) *valid = true;
    if (normalized == QStringLiteral("light")) return UiThemeMode::Light;
    if (normalized == QStringLiteral("dark")) return UiThemeMode::Dark;
    if (normalized == QStringLiteral("system")) return UiThemeMode::System;
    if (valid) *valid = false;
    return UiThemeMode::System;
}
```

在 `retrieveFromStorage()` 的 `view.style` 读取旁边加入：

```cpp
bool themeModeValid = false;
const auto themeModeValue = settings
    .value(QStringLiteral("view.themeMode"), QStringLiteral("system"))
    .toString();
uiThemeMode_ = uiThemeModeFromStorageValue(themeModeValue, &themeModeValid);
if (!themeModeValid) {
    settings.setValue(QStringLiteral("view.themeMode"), QStringLiteral("system"));
}
```

在 `saveToStorage()` 加入：

```cpp
settings.setValue(QStringLiteral("view.themeMode"),
                  uiThemeModeStorageValue(uiThemeMode_));
```

- [ ] **步骤 5：扩充数据行并验证三态**

给测试增加 `readsEveryStableValue_data()` / `readsEveryStableValue()`，数据必须是：

```cpp
QTest::newRow("system") << QStringLiteral("system") << UiThemeMode::System;
QTest::newRow("light") << QStringLiteral("light") << UiThemeMode::Light;
QTest::newRow("dark") << QStringLiteral("dark") << UiThemeMode::Dark;
```

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target zzlogg_theme_configuration_test
& 'D:/SoftWare/CMake/bin/ctest.exe' --test-dir out/build/ui2-contract `
  -C Debug -R zzlogg_ui2.theme_configuration --output-on-failure
```

预期：PASS，4 个主题配置用例全部通过。

- [ ] **步骤 6：提交主题配置**

```powershell
git add CMakeLists.txt src/settings/include/configuration.h `
  src/settings/src/configuration.cpp tests/ui2
git commit -m "feat: 添加 UI2 三态主题配置"
```

### 任务 3：让设置窗口在旧 Style 与新 Theme 之间切换

**文件：**

- 修改：`src/ui/include/optionsdialog.ui`
- 修改：`src/ui/src/optionsdialog.cpp`
- 修改：`src/ui/include/mainwindow.h`
- 修改：`src/ui/src/mainwindow.cpp`
- 创建：`tests/ui2/optionsthemetest.cpp`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：编写失败的设置可见性测试**

新增 `zzlogg_options_theme_test`，链接 `klogg_ui`、`Qt6::Test`、`Qt6::Widgets`，以 `-platform offscreen` 注册 CTest。核心测试：

```cpp
void OptionsThemeTest::showsOnlyTheModeOwnedByTheEntryPoint()
{
    qApp->setProperty("zzlogg.fluentUi", false);
    OptionsDialog legacy;
    QVERIFY(!legacy.findChild<QGroupBox*>(QStringLiteral("styleBox"))->isHidden());
    QVERIFY(legacy.findChild<QGroupBox*>(QStringLiteral("themeBox"))->isHidden());

    qApp->setProperty("zzlogg.fluentUi", true);
    OptionsDialog fluent;
    QVERIFY(fluent.findChild<QGroupBox*>(QStringLiteral("styleBox"))->isHidden());
    QVERIFY(!fluent.findChild<QGroupBox*>(QStringLiteral("themeBox"))->isHidden());
    auto* combo = fluent.findChild<QComboBox*>(QStringLiteral("themeModeComboBox"));
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->itemData(0).toInt(), static_cast<int>(UiThemeMode::System));
    QCOMPARE(combo->itemData(1).toInt(), static_cast<int>(UiThemeMode::Light));
    QCOMPARE(combo->itemData(2).toInt(), static_cast<int>(UiThemeMode::Dark));
}
```

测试源文件包含 `persistentinfo.h` 并定义 `const bool PersistentInfo::ForcePortable = false;`，满足独立测试 exe 的静态成员链接。测试主程序先执行 `QStandardPaths::setTestModeEnabled(true)`，设置唯一 organization/application name，再调用 `Configuration::getSynced()`，禁止读写用户的真实配置。

- [ ] **步骤 2：运行测试确认失败**

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target zzlogg_options_theme_test

$testInfo = & 'D:/SoftWare/CMake/bin/ctest.exe' --test-dir out/build/ui2-contract `
  -C Debug -N -V -R '^zzlogg_ui2.options_theme$'
$runtimeDirs = $testInfo | Select-String 'PATH=path_list_prepend:(.+)$' | ForEach-Object {
  $_.Matches[0].Groups[1].Value
}
$env:PATH = ($runtimeDirs + $env:PATH) -join [IO.Path]::PathSeparator
& 'out/build/ui2-contract/output/Debug/zzlogg_options_theme_test.exe' `
  showsOnlyTheModeOwnedByTheEntryPoint -platform offscreen
```

预期：构建成功；聚焦执行以 exit 1 失败，`showsOnlyTheModeOwnedByTheEntryPoint` 中
`legacyTheme` 断言报告 `themeBox` 不存在（因此 `themeModeComboBox` 也尚不存在）。

- [ ] **步骤 3：在原 Appearance 行内加入 Theme 组**

修改 `optionsdialog.ui`：在 `styleBox` 同一 `horizontalLayout_8` 中增加 `QGroupBox themeBox`，标题为 `Theme`，内部 `QComboBox` 名为 `themeModeComboBox`。不改变字体、语言、High DPI 或其它 tab 的布局。

在 `OptionsDialog` 构造中，`setupUi()` 后配置互斥可见性：

```cpp
const bool fluentUi = qApp->property("zzlogg.fluentUi").toBool();
styleBox->setVisible(!fluentUi);
themeBox->setVisible(fluentUi);
themeModeComboBox->addItem(tr("Use system setting"),
                           static_cast<int>(UiThemeMode::System));
themeModeComboBox->addItem(tr("Light"),
                           static_cast<int>(UiThemeMode::Light));
themeModeComboBox->addItem(tr("Dark"),
                           static_cast<int>(UiThemeMode::Dark));
```

- [ ] **步骤 4：把设置窗口与 Configuration 双向同步**

`updateDialogFromConfig()` 增加：

```cpp
const auto themeIndex = themeModeComboBox->findData(
    static_cast<int>(config.uiThemeMode()));
themeModeComboBox->setCurrentIndex(themeIndex < 0 ? 0 : themeIndex);
```

`updateConfigFromDialog()` 仅在 Fluent 入口写主题，旧入口只写原 Style，避免从旧程序打开设置时覆盖 UI2 的主题：

```cpp
const bool fluentUi = qApp->property("zzlogg.fluentUi").toBool();
if (fluentUi) {
    config.setUiThemeMode(static_cast<UiThemeMode>(
        themeModeComboBox->currentData().toInt()));
} else {
    restartAppMessage = config.style() != styleComboBox->currentText();
    config.setStyle(styleComboBox->currentText());
}
```

- [ ] **步骤 5：发出应用级主题变更语义信号**

在 `MainWindow` 信号区增加：

```cpp
void uiThemeChanged(UiThemeMode mode);
```

在 `MainWindow::options()` 现有 `OptionsDialog::optionsChanged` lambda 的末尾加入：

```cpp
Q_EMIT uiThemeChanged(config.uiThemeMode());
```

这个信号不依赖 ZzPureTools，因此 `klogg_ui` 仍保持原链接边界。

- [ ] **步骤 6：运行设置测试与旧目标构建**

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target zzlogg_options_theme_test klogg klogg_portable
& 'D:/SoftWare/CMake/bin/ctest.exe' --test-dir out/build/ui2-contract `
  -C Debug -R 'zzlogg_ui2.(theme_configuration|options_theme)' --output-on-failure
```

预期：PASS；旧目标仍不链接 `ZzFluentUI` / `ZzWindowKit`，设置测试确认两种入口不会同时显示 Style 和 Theme。

- [ ] **步骤 7：提交设置入口**

```powershell
git add src/ui/include/optionsdialog.ui src/ui/src/optionsdialog.cpp `
  src/ui/include/mainwindow.h src/ui/src/mainwindow.cpp `
  tests/ui2/optionsthemetest.cpp tests/ui2/CMakeLists.txt
git commit -m "feat: 在 UI2 设置中加入主题选择"
```

### 任务 4：抽取共享应用 runner 并为所有窗口增加装饰回调

**文件：**

- 创建：`src/app/applicationrunner.h`
- 创建：`src/app/applicationrunner.cpp`
- 修改：`src/app/main.cpp`
- 修改：`src/app/kloggapp.h`
- 修改：`src/app/CMakeLists.txt`

- [ ] **步骤 1：构建并运行旧入口基线**

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target klogg klogg_portable
& 'D:/SoftWare/CMake/bin/ctest.exe' --test-dir out/build/ui2-contract `
  -C Debug -R '^klogg_smoke$' --output-on-failure
```

预期：PASS；记录 `klogg.exe --version` 输出，重构后必须完全一致。

- [ ] **步骤 2：定义 runner 的精确接口**

`applicationrunner.h`：

```cpp
#pragma once
#include <functional>
#include <memory>
#include <QString>

class KloggApp;
class QObject;

using KloggUiRuntimeFactory =
    std::function<std::unique_ptr<QObject>(KloggApp&, QString* error)>;

struct KloggApplicationOptions final {
    KloggUiRuntimeFactory createUiRuntime;
    QString startupWarning;
};

int runKloggApplication(int argc, char* argv[],
                        KloggApplicationOptions options = {});
```

- [ ] **步骤 3：原样移动启动流程并显式管理 Style**

把当前 `main.cpp` 中 `setApplicationAttributes()` 和主流程移动到 `applicationrunner.cpp`。Configuration、application attributes、`KloggApp`、CLI 和日志初始化仍按原顺序；Runtime/Style 只在 primary 分支、首个窗口创建前选择：

```cpp
const auto& config = Configuration::getSynced();
setApplicationAttributes(config.enableQtHighDpi(), config.scaleFactorRounding());
KloggApp app(argc, argv);
std::unique_ptr<QObject> uiRuntime;
QString runtimeError;

if (!parameters.multi_instance && app.isSecondary()) {
    app.sendFilesToPrimaryInstance(parameters.filenames);
} else {
    if (options.createUiRuntime) {
        uiRuntime = options.createUiRuntime(app, &runtimeError);
    }
    if (!uiRuntime) {
        StyleManager::applyStyle(config.style());
    }
}
```

`uiRuntime` 必须在 primary/secondary 分支之前声明并活过 `app.exec()`。将 `options.startupWarning` 和 `runtimeError` 合并成 `warning`，用 `QTimer::singleShot(0, &app, [warning] { QMessageBox::warning(nullptr, QObject::tr("ZzLogg UI"), warning); })` 在事件循环启动后显示；同时通过 `LOG_ERROR` 记录技术文本。原命令行、日志、allocator、session、文件加载和后台任务代码按原顺序保留。

- [ ] **步骤 4：把旧 main 缩成薄入口**

`main.cpp` 只保留 portable 常量定义和调用：

```cpp
#include "applicationrunner.h"
#include "persistentinfo.h"

#ifdef KLOGG_PORTABLE
const bool PersistentInfo::ForcePortable = true;
#else
const bool PersistentInfo::ForcePortable = false;
#endif

int main(int argc, char* argv[])
{
    return runKloggApplication(argc, argv);
}
```

- [ ] **步骤 5：为 KloggApp 增加可选窗口装饰器**

`kloggapp.h` 公共区增加：

```cpp
#include <functional>

using WindowDecorator = std::function<void(MainWindow&)>;
void setWindowDecorator(WindowDecorator decorator)
{
    windowDecorator_ = std::move(decorator);
}
QList<MainWindow*> mainWindows() const
{
    QList<MainWindow*> result;
    for (const auto& entry : mainWindows_) result.push_back(entry.second);
    return result;
}
```

私有 `newWindow(WindowSession&&)` 在 `new MainWindow(session)` 完成、任何 `show()` 发生前调用：

```cpp
auto& window = mainWindows_.back().second;
if (windowDecorator_) {
    windowDecorator_(*window);
}
```

类成员加入：

```cpp
WindowDecorator windowDecorator_;
```

这条路径同时覆盖普通新窗口、恢复 session 的窗口和 `File -> New Window`。

- [ ] **步骤 6：调整 app 目标源集并验证无行为变化**

`src/app/CMakeLists.txt` 使用同一个公共源集，`klogg_grep` 源集不变：

```cmake
set(KLOGG_UI_COMMON_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/messagereceiver.h
  ${CMAKE_CURRENT_SOURCE_DIR}/kloggapp.h
  ${CMAKE_CURRENT_SOURCE_DIR}/applicationrunner.h
  ${CMAKE_CURRENT_SOURCE_DIR}/applicationrunner.cpp
  ${DOCUMENTATION_RESOURCE}
  ${ICON_FILE})
add_executable(klogg ${OS_BUNDLE} ${MAIN_SOURCES}
  ${KLOGG_UI_COMMON_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
add_executable(klogg_portable ${OS_BUNDLE} ${MAIN_SOURCES}
  ${KLOGG_UI_COMMON_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
```

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target klogg klogg_portable
out/build/ui2-contract/output/Debug/klogg.exe --version
& 'D:/SoftWare/CMake/bin/ctest.exe' --test-dir out/build/ui2-contract -C Debug -R '^klogg_smoke$' --output-on-failure
```

预期：构建和 smoke PASS，版本输出与步骤 1 相同。

- [ ] **步骤 7：提交 runner 重构**

```powershell
git add src/app/applicationrunner.h src/app/applicationrunner.cpp `
  src/app/main.cpp src/app/kloggapp.h src/app/CMakeLists.txt
git commit -m "refactor: 抽取共享 GUI 应用启动流程"
```

### 任务 5：实现事务化 Fluent Shell

**文件：**

- 创建：`src/ui2/CMakeLists.txt`
- 创建：`src/ui2/include/zzloggfluentshell.h`
- 创建：`src/ui2/src/zzloggfluentshell.cpp`
- 修改：`src/CMakeLists.txt`
- 创建：`tests/ui2/fluentshelltest.cpp`
- 创建：`tests/ui2/windowkittestmain.cpp`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：先写标题和 Shell 行为测试**

测试必须覆盖：

```cpp
QCOMPARE(formatZzLoggWindowTitle({}), QStringLiteral("ZzLogg"));
QCOMPARE(formatZzLoggWindowTitle(QStringLiteral("server.log")),
         QStringLiteral("server.log \u2014 ZzLogg"));
```

构造一个带 `QMenuBar`、两个 `QMenu`、`QToolBar` 和 central widget 的 `QMainWindow`，保存所有原指针，再安装 Shell。断言：

```cpp
QVERIFY(result);
QVERIFY(window.property("zzlogg.fluentShellInstalled").toBool());
QCOMPARE(window.centralWidget(), originalCentral);
QCOMPARE(window.findChild<QToolBar*>(QStringLiteral("fixtureToolbar")), originalToolbar);
QCOMPARE(window.menuWidget(), static_cast<QWidget*>(titleBar));
QVERIFY(originalMenuBar->isHidden());
QCOMPARE(titleBar->menuBar()->actions(), originalTopLevelActions);
QCOMPARE(titleBar->menuBar()->actions().at(0)->menu(), originalFileMenu);
```

另一个用例注入下面的 `ChromeConfigurator`，模拟 attach 成功、configure 失败：

```cpp
auto failAfterAttach = [](QMainWindow& window,
                          ZzFluentUI::ZzFluentTitleBar&,
                          ZzWindowKit::ZzWindowAgent& agent) {
    auto attached = agent.attach(&window);
    if (!attached) return attached;
    return ZzCore::ZzResult<void>::failure(ZzCore::ZzError(
        ZzCore::ZzErrorCode::Backend,
        QStringLiteral("injected configure failure")));
};
```

断言安装失败、窗口 flags 恢复为安装前值、`window.menuBar()` 仍是原对象、所有 QAction 仍可触发、安装属性为 false。标题数据还要覆盖 Unicode 文件名和 512 个字符的长文件名；长文件名的逻辑标题完整交给标题栏，由 `ZzFluentTitleBar` 负责末尾省略。

- [ ] **步骤 2：写 WindowKit 前置的测试 main 并确认失败**

`windowkittestmain.cpp` 必须先 Bootstrap，再创建 `QApplication`：

```cpp
int main(int argc, char* argv[])
{
    const auto prepared = ZzWindowKit::ZzWindowKitBootstrap::prepare();
    if (!prepared) return 2;
    QApplication app(argc, argv);
    FluentShellTest test;
    return QTest::qExec(&test, argc, argv);
}
```

`tests/ui2/CMakeLists.txt` 建立测试目标：

```cmake
add_executable(zzlogg_fluent_shell_test
  windowkittestmain.cpp fluentshelltest.cpp)
target_link_libraries(zzlogg_fluent_shell_test PRIVATE
  zzlogg_ui2_shell Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_fluent_shell_test PROPERTIES AUTOMOC ON)
klogg_copy_ui2_runtime_dlls(zzlogg_fluent_shell_test)
add_test(NAME zzlogg_ui2.fluent_shell
         COMMAND zzlogg_fluent_shell_test)
```

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target zzlogg_fluent_shell_test
```

预期：FAIL，Shell 文件和 `formatZzLoggWindowTitle()` 尚不存在。

- [ ] **步骤 3：定义 Shell 的最小公开接口**

`zzloggfluentshell.h`：

```cpp
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
namespace ZzFluentUI { class ZzFluentTitleBar; class ZzThemeController; }
namespace ZzWindowKit { class ZzWindowAgent; }

[[nodiscard]] QString formatZzLoggWindowTitle(const QString& documentName);

class ZzLoggFluentShell final : public QObject {
    Q_OBJECT
  public:
    using ChromeConfigurator = std::function<ZzCore::ZzResult<void>(
        QMainWindow&, ZzFluentUI::ZzFluentTitleBar&,
        ZzWindowKit::ZzWindowAgent&)>;
    static ZzCore::ZzResult<ZzLoggFluentShell*> install(
        QMainWindow& window,
        ZzFluentUI::ZzThemeController& theme,
        ChromeConfigurator chromeConfigurator = {});
    ~ZzLoggFluentShell() override;
    void setActiveDocumentName(const QString& documentName);
  Q_SIGNALS:
    void themeModeRequested(ZzFluentUI::ZzThemeMode mode);
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
  private:
    ZzLoggFluentShell(QMainWindow& window,
                      QMenuBar* originalMenuBar,
                      ZzFluentUI::ZzFluentTitleBar* titleBar,
                      std::unique_ptr<ZzWindowKit::ZzWindowAgent> agent,
                      ZzFluentUI::ZzThemeController& theme);
    void syncWindowState();
    void syncTheme();
    void setAlwaysOnTop(bool requested);
    QPointer<QMainWindow> window_;
    QPointer<QMenuBar> originalMenuBar_;
    QPointer<ZzFluentUI::ZzFluentTitleBar> titleBar_;
    std::unique_ptr<ZzWindowKit::ZzWindowAgent> agent_;
    QPointer<ZzFluentUI::ZzThemeController> theme_;
};
```

`src/ui2/CMakeLists.txt` 建立 `zzlogg_ui2_shell` 静态库，只链接：

```cmake
add_library(zzlogg_ui2_shell STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/zzloggfluentshell.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/zzloggfluentshell.cpp)
target_include_directories(zzlogg_ui2_shell
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(zzlogg_ui2_shell PUBLIC
  Zz::WindowKit
  Zz::FluentFoundation
  Zz::FluentUI
  Qt6::Widgets)
target_compile_features(zzlogg_ui2_shell PUBLIC cxx_std_20)
set_target_properties(zzlogg_ui2_shell PROPERTIES AUTOMOC ON)
```

`src/CMakeLists.txt` 在 `app` 之前、仅当 `KLOGG_BUILD_UI2` 为真时 `add_subdirectory(ui2)`。

- [ ] **步骤 4：实现菜单迁移事务与 WindowKit 绑定**

`install()` 按以下固定顺序执行：

1. 保存 `QMenuBar* originalMenuBar = window.menuBar()` 和 `originalMenuBar->actions()`。
2. 创建以 `window` 为 parent 的 `ZzFluentTitleBar`，objectName 为 `zzloggFluentTitleBar`，菜单模式设为 `ZzFluentUI::ZzTitleBarMenuDisplayMode::Adaptive`；此时不改原菜单栏。
3. Shell 创建它最终独占的 `ZzWindowAgent`；默认 `ChromeConfigurator` 依次调用 `attach(&window)` 和 `configureChrome(chrome)`，测试可以注入返回失败结果的 configurator。
4. `chrome.titleBar`、`windowIcon`、`interactiveWidgets` 取自标题栏；仅在没有 `NativeSystemButtons` capability 时填写三个按钮。
5. attach/configure 任一步失败，删除临时标题栏和 agent；原菜单从未被修改，不需要恢复空菜单。
6. 所有可能失败的 WindowKit 步骤成功后，遍历原顶层 QAction：有 `QMenu` 的 action 把该 QMenu reparent 到标题栏 `QMenuBar`，顶层 separator/action reparent 到新 `QMenuBar`，再按原顺序 `addAction()`；绝不复制 QMenu/QAction 对象。
7. 清空并隐藏原 `QMenuBar`，`window.setMenuWidget(titleBar)`；创建以 `window` 为 QObject parent 的 Shell，设置 `zzlogg.fluentShellInstalled=true`，再连接信号。标准 Qt 所有权操作之后没有返回失败的步骤，因此菜单迁移是事务的提交点。

chrome 核心代码：

```cpp
const bool nativeButtons = agent->capabilities().testFlag(
    ZzWindowKit::ZzWindowCapability::NativeSystemButtons);
titleBar->setSystemButtonsVisible(!nativeButtons);
ZzWindowKit::ZzWindowChromeConfiguration chrome;
chrome.titleBar = titleBar;
chrome.windowIcon = titleBar->windowIconWidget();
chrome.interactiveWidgets = titleBar->hitTestVisibleWidgets();
if (!nativeButtons) {
    chrome.minimizeButton = titleBar->minimizeButton();
    chrome.maximizeButton = titleBar->maximizeButton();
    chrome.closeButton = titleBar->closeButton();
}
```

- [ ] **步骤 5：实现标题、窗口按钮、图标与置顶同步**

标题函数只接收已经去路径的活动文档名：

```cpp
return documentName.isEmpty()
    ? QStringLiteral("ZzLogg")
    : QStringLiteral("%1 \u2014 ZzLogg").arg(documentName);
```

`setActiveDocumentName()` 同时调用 `window.setWindowTitle()` 和 `titleBar.setTitle()`。安装成功后先调用一次 `setActiveDocumentName({})`，保证首次无文件窗口立即显示 `ZzLogg`；恢复/加载文件产生的语义信号再覆盖它。事件过滤器在 `QEvent::WindowStateChange` 与 `QEvent::WindowIconChange` 时分别同步最大化状态和图标。

窗口按钮连接到 `showMinimized()`、最大化/还原 lambda、`close()`。置顶实现严格保留可见性和 windowState：

```cpp
const bool wasVisible = window_->isVisible();
const auto previousState = window_->windowState();
window_->setWindowFlag(Qt::WindowStaysOnTopHint, requested);
window_->setWindowState(previousState);
wasVisible ? window_->show() : window_->hide();
titleBar_->setAlwaysOnTop(
    window_->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
```

置顶状态不写入 `Configuration`。

- [ ] **步骤 6：实现应用主题观察但不在 Shell 中持久化**

Shell 把 `ZzFluentTitleBar::themeModeRequested` 原样转发为自己的信号；监听 `ZzThemeController::snapshotChanged` 后执行：

```cpp
titleBar_->setThemeMode(theme_->mode());
```

高对比度由 `ZzThemeController` 的 System 解析和快照处理，Shell 不增加第四个持久化模式。

- [ ] **步骤 7：扩充并运行 Shell 测试**

增加测试：两个 Shell 观察同一 Controller，`setMode(Dark)` 后两个标题栏和一个普通 `QLineEdit` 的 palette 均更新；模拟 `alwaysOnTopRequested(true/false)`；模拟最小化、最大化/还原、关闭意图；销毁窗口后没有存活 Shell 指针。菜单用例还要逐项断言 separator、shortcut、checked、enabled 和 QAction 地址保持一致，并验证 `hitTestVisibleWidgets()` 包含菜单交互控件、不包含标题栏空白本身。

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target zzlogg_fluent_shell_test
& 'D:/SoftWare/CMake/bin/ctest.exe' --test-dir out/build/ui2-contract `
  -C Debug -R zzlogg_ui2.fluent_shell --output-on-failure
```

预期：PASS；Windows 测试不得设置 `QT_QPA_PLATFORM=offscreen`，以便覆盖真实 WindowKit backend。Linux/macOS 编译门只要求目标完成编译，桌面 runner 可额外执行该测试。

- [ ] **步骤 8：提交 Shell**

```powershell
git add src/CMakeLists.txt src/ui2 tests/ui2
git commit -m "feat: 添加事务化 Fluent 窗口外壳"
```

### 任务 6：连接 MainWindow 语义信号、全局 Runtime 与 UI2 入口

**文件：**

- 修改：`src/ui/include/mainwindow.h`
- 修改：`src/ui/src/mainwindow.cpp`
- 创建：`src/ui2/include/zzlogguiruntime.h`
- 创建：`src/ui2/src/zzlogguiruntime.cpp`
- 修改：`src/ui2/CMakeLists.txt`
- 创建：`src/app/main_ui2.cpp`
- 修改：`src/app/CMakeLists.txt`

- [ ] **步骤 1：为活动文档标题编写失败的语义测试**

在 Shell 测试中增加一个 `QSignalSpy` 契约：Runtime 装饰窗口后，输入空文档名得到 `ZzLogg`，输入 `server.log` 得到 `server.log — ZzLogg`。在应用冒烟测试中还要验证切换两个真实文件 tab 会更新标题。

运行现有 UI2 测试，预期应用级用例因 `MainWindow::activeDocumentNameChanged` 和 Runtime 尚不存在而不能编译。

- [ ] **步骤 2：让 MainWindow 发出活动文档名而不改变旧标题**

在信号区增加：

```cpp
void activeDocumentNameChanged(const QString& fileName);
```

`MainWindow::updateTitleBar()` 保留当前旧窗口标题计算与 `setWindowTitle()`，末尾增加：

```cpp
Q_EMIT activeDocumentNameChanged(
    file_name.isEmpty() ? QString{} : strippedName(file_name));
```

这样旧 `klogg` 的 `Untitled - klogg #N (build <当前版本>)` 格式完全不变；只有 Shell 收到语义值后改写 UI2 标题。

- [ ] **步骤 3：定义 Runtime API 和主题映射**

`zzlogguiruntime.h`：

```cpp
#pragma once
#include <cstdint>
#include <memory>
#include <QObject>
#include "configuration.h"

class KloggApp;
class MainWindow;
namespace ZzFluentUI { class ZzThemeController; enum class ZzThemeMode : std::uint8_t; }

class ZzLoggUiRuntime final : public QObject {
    Q_OBJECT
  public:
    static std::unique_ptr<ZzLoggUiRuntime> create(
        KloggApp& app, QString* error);
    ~ZzLoggUiRuntime() override;
  private:
    explicit ZzLoggUiRuntime(KloggApp& app);
    void decorate(MainWindow& window);
    void applyTheme(UiThemeMode mode, bool persist);
    KloggApp* app_ = nullptr;
    std::unique_ptr<ZzFluentUI::ZzThemeController> theme_;
};
```

在 `.cpp` 内部实现封闭映射：System/Light/Dark 对应同名 `ZzThemeMode`；收到 `HighContrast` 请求时映射为 `UiThemeMode::System`，因为高对比度只由系统模式解析。

- [ ] **步骤 4：安装应用级 Fluent Style 并保证析构顺序**

`create()` 在 `KloggApp` 已构造后：

```cpp
app.setProperty("zzlogg.fluentUi", true);
theme_ = std::make_unique<ZzFluentUI::ZzThemeController>();
theme_->setMode(toZzThemeMode(Configuration::get().uiThemeMode()));
app.setStyle(new ZzFluentUI::ZzFluentStyle(theme_.get()));
app.setWindowDecorator([this](MainWindow& window) { decorate(window); });
```

`create()` 用 `try/catch (const std::exception&)` 包住 Controller 和 Style 构造；失败时把异常文本写入 `error`、清除 `zzlogg.fluentUi` 属性并返回空指针，runner 随即应用旧 `StyleManager`/Qt palette。析构函数先清除 decorator，再用 `QStyleFactory::create("Fusion")` 替换 Fluent Style，最后销毁 `theme_`，保证应用拥有的 Style 不会观察已经销毁的 Controller。

- [ ] **步骤 5：装饰每个窗口并连接同一配置源**

`decorate()` 调用 `ZzLoggFluentShell::install(window, *theme_)`。Shell 在安装成功时用 Debug 日志输出 `ZzWindowAgent::capabilities()`；Runtime 在失败时输出 ZzError code、`technicalMessage()` 和 `context()`。成功后连接：

```cpp
connect(&window, &MainWindow::activeDocumentNameChanged,
        shell, &ZzLoggFluentShell::setActiveDocumentName);
connect(&window, &MainWindow::uiThemeChanged, this,
        [this](UiThemeMode mode) { applyTheme(mode, false); });
connect(shell, &ZzLoggFluentShell::themeModeRequested, this,
        [this](ZzFluentUI::ZzThemeMode mode) {
            applyTheme(fromZzThemeMode(mode), true);
        });
```

标题栏请求时 `persist=true`：写 `Configuration::get().setUiThemeMode(mode)`、`save()`，再调用 `theme_->setMode(toZzThemeMode(mode))`。设置窗口已经保存配置，因此 `persist=false` 时只更新 Controller。安装失败时记录完整 ZzError，并用 `QTimer::singleShot(0, &window, [&window, message] { QMessageBox::warning(&window, QObject::tr("ZzLogg UI"), message); })` 显示一次；原菜单已由事务回滚。

- [ ] **步骤 6：实现 UI2 Bootstrap 入口**

`main_ui2.cpp`：

```cpp
#include <ZzCore/ZzError.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>
#include "applicationrunner.h"
#include "persistentinfo.h"
#include "zzlogguiruntime.h"

const bool PersistentInfo::ForcePortable = false;

int main(int argc, char* argv[])
{
    KloggApplicationOptions options;
    const auto bootstrap = ZzWindowKit::ZzWindowKitBootstrap::prepare();
    if (!bootstrap) {
        options.startupWarning = QStringLiteral("Fluent window initialization failed: %1")
            .arg(bootstrap.error().technicalMessage());
    } else {
        options.createUiRuntime = [](KloggApp& app, QString* error) {
            return ZzLoggUiRuntime::create(app, error);
        };
    }
    return runKloggApplication(argc, argv, std::move(options));
}
```

Bootstrap 失败不是进程失败：runner 无 Runtime 时应用旧 `StyleManager`，继续创建原生 Klogg 窗口并显示警告。

- [ ] **步骤 7：建立 `zzlogg_ui2` 目标与 Windows 运行时复制**

在 `src/app/CMakeLists.txt` 仅当 `KLOGG_BUILD_UI2` 为真时：

```cmake
add_executable(zzlogg_ui2 ${OS_BUNDLE}
  ${MAIN_SOURCES}
  ${KLOGG_UI_COMMON_SOURCES}
  ${CMAKE_CURRENT_SOURCE_DIR}/main_ui2.cpp)
set_target_properties(zzlogg_ui2 PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(zzlogg_ui2 PRIVATE cxx_std_20)
target_link_libraries(zzlogg_ui2 PRIVATE
  ${MAIN_LIBS} klogg_ui zzlogg_ui2_runtime)
```

`zzlogg_ui2_runtime` 在 `src/ui2/CMakeLists.txt` 使用以下边界，显式取得 header-only `KloggApp` 所需的 app include path 和依赖：

```cmake
add_library(zzlogg_ui2_runtime STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/zzlogguiruntime.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/zzlogguiruntime.cpp)
target_include_directories(zzlogg_ui2_runtime
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(zzlogg_ui2_runtime
  PUBLIC zzlogg_ui2_shell klogg_settings
  PRIVATE klogg_ui klogg_crash_handler kdsingleapp)
target_compile_features(zzlogg_ui2_runtime PUBLIC cxx_std_20)
set_target_properties(zzlogg_ui2_runtime PROPERTIES AUTOMOC ON)
```

Windows 调用任务 1 的复用函数，保证输出目录中的 exe 可直接运行：

```cmake
klogg_copy_ui2_runtime_dlls(zzlogg_ui2)
```

不要增加 `zzlogg_ui2_portable` 或把 UI2 放进 `klogg_portable_folder`。

- [ ] **步骤 8：构建三个 GUI 目标并人工启动 UI2**

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target klogg klogg_portable zzlogg_ui2
out/build/ui2-contract/output/Debug/zzlogg_ui2.exe --multi --new-session
```

预期：旧两个 exe 正常构建；UI2 显示“图标、菜单、绝对居中标题、主题按钮、置顶按钮、窗口按钮”，中心工具栏、文件 tab、原日志视图、搜索栏、过滤结果视图、splitter、status bar 的相对位置不变。

- [ ] **步骤 9：提交 Runtime 与入口**

```powershell
git add src/ui/include/mainwindow.h src/ui/src/mainwindow.cpp `
  src/ui2 src/app/main_ui2.cpp src/app/CMakeLists.txt
git commit -m "feat: 添加 ZzLogg UI2 应用入口"
```

### 任务 7：增加全应用冒烟、tab/搜索和多窗口验证

**文件：**

- 修改：`src/ui/src/mainwindow.cpp`
- 修改：`src/ui/src/crawlerwidget.cpp`
- 修改：`src/app/applicationrunner.cpp`
- 创建：`tests/ui2/fixtures/ui2-first.log`
- 创建：`tests/ui2/fixtures/ui2-second.log`
- 创建：`tests/ui2/sessionrestoresmoke.cmake`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：创建确定性的日志 fixture**

`ui2-first.log`：

```text
INFO startup complete
ERROR first failure
INFO shutdown complete
```

`ui2-second.log`：

```text
DEBUG worker started
ERROR second failure
WARN retry scheduled
```

- [ ] **步骤 2：为现有控件增加非视觉 objectName**

在现有对象构造后设置：

```cpp
mainTabWidget_.setObjectName(QStringLiteral("documentTabs"));
searchLineEdit_->setObjectName(QStringLiteral("mainSearchEdit"));
searchButton_->setObjectName(QStringLiteral("mainSearchButton"));
tabbedFilteredView_->setObjectName(QStringLiteral("filteredResultsTabs"));
```

不增加控件、不改 layout、不改 size policy。

- [ ] **步骤 3：先注册失败的应用冒烟测试**

`tests/ui2/CMakeLists.txt`：

```cmake
add_test(NAME zzlogg_ui2.application_smoke
  COMMAND $<TARGET_FILE:zzlogg_ui2>
          --multi --new-session
          ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui2-first.log
          ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui2-second.log)
set(ui2_smoke_config "${CMAKE_CURRENT_BINARY_DIR}/application-smoke-config")
file(MAKE_DIRECTORY "${ui2_smoke_config}")
if(WIN32)
  set(ui2_smoke_config_env "APPDATA=${ui2_smoke_config}")
else()
  set(ui2_smoke_config_env "XDG_CONFIG_HOME=${ui2_smoke_config}")
endif()
set_tests_properties(zzlogg_ui2.application_smoke PROPERTIES
  TIMEOUT 30
  ENVIRONMENT "ZZLOGG_UI2_SMOKE_MS=1800;${ui2_smoke_config_env}")
```

Linux/macOS 无桌面 runner 时额外设置 `QT_QPA_PLATFORM=offscreen`；Windows 验收保留默认 `windows` 平台插件，禁止用 offscreen 掩盖 WindowKit 回退。

运行测试，预期 FAIL/TIMEOUT，因为应用尚不会自动验证并退出。

- [ ] **步骤 4：在共享 runner 中加入仅由环境变量启用的 smoke probe**

正常用户未设置 `ZZLOGG_UI2_SMOKE_MS` 时不执行任何 probe。变量为正且 `app.property("zzlogg.fluentUi")` 为 true 时，在两个文件加载后：

测试进程在 Runtime factory 执行前把测试专用 Configuration 的主题重置为 `UiThemeMode::System` 并保存，保证重复运行时仍能观察从 System 到 Dark 的传播；普通进程不执行重置。

1. 用 `app.mainWindows()` 取得第一个窗口，创建并显示第二个窗口。
2. 验证两个窗口均有 `zzlogg.fluentShellInstalled=true`、非空 centralWidget 和 `zzloggFluentTitleBar`。
3. 找到第一个窗口的 `documentTabs`，等待 count 为 2，依次切换 index 0/1，验证窗口标题分别包含 `ui2-first.log` / `ui2-second.log` 并以 `— ZzLogg` 结尾。
4. 找到当前 `CrawlerWidget` 内的 `mainSearchEdit`、`mainSearchButton`、`filteredResultsTabs`；设置 `ERROR`、调用 `click()`，等待搜索按钮恢复可见，验证搜索文本仍是 `ERROR` 且 filtered tabs count 至少为 1。
5. 在第一个标题栏找到 objectName 为 `zzTitleBarThemeMenu` 的 `QMenu`，触发 data 等于 `static_cast<int>(UiThemeMode::Dark)` 的 QAction；验证 `Configuration::get().uiThemeMode()` 已保存为 Dark，两个标题栏的 `themeMode` 属性同步变化且相等。
6. 所有条件成立调用 `app.exit(EXIT_SUCCESS)`；任何对象缺失、Shell 回退、标题不同、主题不同或搜索未完成调用 `app.exit(EXIT_FAILURE)` 并 `LOG_ERROR` 输出失败条件。

异步轮询使用 `QElapsedTimer` + 50 ms `QTimer`，总截止时间取环境变量；不得调用阻塞 sleep。

`ZZLOGG_UI2_SMOKE_MODE` 为空时执行上面的交互 probe；`seed-session` 时创建第二个窗口并对 `app.mainWindows()` 的快照逐个调用 `close()`，让现有 `MainWindow::closeEvent()` 写入 session；`verify-restored` 时禁止主动创建窗口，只等待 `reloadSession()` 完成并验证恢复窗口数至少为 2、每个窗口 Shell 均已安装。

- [ ] **步骤 5：运行应用冒烟与全部 UI2 测试**

运行：

```powershell
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/ui2-contract `
  --config Debug --target zzlogg_ui2
& 'D:/SoftWare/CMake/bin/ctest.exe' --test-dir out/build/ui2-contract -C Debug `
  -R '^zzlogg_ui2\.' --output-on-failure
```

预期：主题配置、设置入口、Shell、应用 smoke 全部 PASS；smoke 证明没有 runtime fallback，并实际覆盖两个文件 tab、一次过滤搜索、第二窗口装饰和跨窗口主题同步。

- [ ] **步骤 6：验证旧 UI 不响应 smoke 环境变量**

运行：

```powershell
$env:ZZLOGG_UI2_SMOKE_MS='300'
$process = Start-Process -FilePath 'out/build/ui2-contract/output/Debug/klogg.exe' `
  -ArgumentList '--multi','--new-session' -PassThru
Start-Sleep -Milliseconds 800
if ($process.HasExited) { throw 'legacy klogg unexpectedly entered UI2 smoke mode' }
Stop-Process -Id $process.Id
Remove-Item Env:ZZLOGG_UI2_SMOKE_MS
```

预期：旧 `klogg` 不读取 UI2 smoke 开关；该步骤只终止本步骤明确启动并记录 PID 的测试进程。

- [ ] **步骤 7：增加会话恢复的双进程 smoke**

创建 `tests/ui2/sessionrestoresmoke.cmake`，在测试专用配置目录中分两次启动同一个 `zzlogg_ui2`：第一次设置 `ZZLOGG_UI2_SMOKE_MODE=seed-session`，打开两个窗口后逐个 `close()` 以写入 session；第二次设置 `ZZLOGG_UI2_SMOKE_MODE=verify-restored` 并传 `--multi --load-session`。verify 阶段要求恢复至少两个窗口，而且每个窗口都有 `zzlogg.fluentShellInstalled=true`，然后以 0 退出；任一条件不成立以非零退出。

CTest 用 `cmake -E env` 为两次进程设置同一个测试专用 `APPDATA`（Windows）或 `XDG_CONFIG_HOME`（Linux/macOS），不得读写用户配置。脚本的进程判定骨架是：

```cmake
file(REMOVE_RECURSE "${TEST_CONFIG_DIR}")
file(MAKE_DIRECTORY "${TEST_CONFIG_DIR}")
if(WIN32)
  set(config_env "APPDATA=${TEST_CONFIG_DIR}")
else()
  set(config_env "XDG_CONFIG_HOME=${TEST_CONFIG_DIR}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "${config_env}"
          ZZLOGG_UI2_SMOKE_MODE=seed-session ZZLOGG_UI2_SMOKE_MS=3000
          "${APP}" --multi --new-session
  RESULT_VARIABLE seed_result TIMEOUT 15)
if(NOT seed_result EQUAL 0)
  message(FATAL_ERROR "UI2 session seed failed: ${seed_result}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "${config_env}"
          ZZLOGG_UI2_SMOKE_MODE=verify-restored ZZLOGG_UI2_SMOKE_MS=3000
          "${APP}" --multi --load-session
  RESULT_VARIABLE restore_result TIMEOUT 15)
if(NOT restore_result EQUAL 0)
  message(FATAL_ERROR "UI2 session restore failed: ${restore_result}")
endif()
```

注册名为 `zzlogg_ui2.session_restore_smoke`，命令传 `-DAPP=$<TARGET_FILE:zzlogg_ui2>` 和 `-DTEST_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/session-smoke-config`，超时 45 秒。

运行：

```powershell
& 'D:/SoftWare/CMake/bin/ctest.exe' --test-dir out/build/ui2-contract `
  -C Debug -R '^zzlogg_ui2.session_restore_smoke$' --output-on-failure
```

预期：PASS，恢复出的每个窗口都成功安装 Shell。

- [ ] **步骤 8：提交冒烟覆盖**

```powershell
git add src/ui/src/mainwindow.cpp src/ui/src/crawlerwidget.cpp `
  src/app/applicationrunner.cpp tests/ui2
git commit -m "test: 覆盖 UI2 工作区与多窗口冒烟"
```

### 任务 8：补齐跨平台 Presets、构建文档和最终验收

**文件：**

- 修改：`CMakePresets.json`
- 修改：`CMakeUserPresets.json.example`
- 修改：`docs/BUILD.md`

- [ ] **步骤 1：增加仓库级 UI2 presets**

在 `CMakePresets.json` 增加隐藏配置：

```json
{
  "name": "ui2-base",
  "hidden": true,
  "cacheVariables": {
    "KLOGG_BUILD_UI2": true,
    "KLOGG_BUILD_UI2_TESTS": true,
    "KLOGG_BUILD_TESTS": false,
    "KLOGG_OSX_DEPLOYMENT_TARGET": "13.3"
  }
}
```

configure presets 增加：

```json
{
  "name": "ninja-ui2-debug",
  "displayName": "Ninja UI2 Debug",
  "inherits": ["ninja-debug", "ui2-base"],
  "binaryDir": "${sourceDir}/out/build/ninja-ui2-debug"
},
{
  "name": "windows-vs2026-ui2",
  "displayName": "Visual Studio 2026 UI2 x64",
  "inherits": ["windows-vs2026", "ui2-base"],
  "binaryDir": "${sourceDir}/out/build/windows-vs2026-ui2"
}
```

build presets 建立 `ninja-ui2-debug`、`windows-vs2026-ui2-debug`、`windows-vs2026-ui2-relwithdebinfo`，分别引用同名 configure preset并为 VS 两项设置 configuration。test presets 使用相同名称和 configuration，并加入：

```json
"filter": { "include": { "name": "^zzlogg_ui2\\." } },
"output": { "outputOnFailure": true }
```

workflow presets 建立 `ninja-ui2-debug`、`windows-vs2026-ui2-debug`、`windows-vs2026-ui2-relwithdebinfo`，每个都按 configure、build、test 三步引用对应 preset。这样 UI2 流程不会触发缺失 `backward-cpp` 的旧测试目标。

- [ ] **步骤 2：扩充用户 preset 示例，不提交机器路径**

`CMakeUserPresets.json.example` 增加：

```json
{
  "name": "windows-qt6-ui2",
  "displayName": "Windows Qt 6 UI2",
  "inherits": "windows-qt6",
  "binaryDir": "${sourceDir}/out/build/windows-qt6-ui2",
  "cacheVariables": {
    "KLOGG_BUILD_UI2": true,
    "KLOGG_BUILD_UI2_TESTS": true
  }
}
```

增加 build presets `windows-qt6-ui2-debug` 和 `windows-qt6-ui2-relwithdebinfo`，两者的 configurePreset 都是 `windows-qt6-ui2`，configuration 分别是 Debug 和 RelWithDebInfo；增加同名 test presets，带 `^zzlogg_ui2\\.` filter；增加同名 workflow presets，按 configure、build、test 三步执行。示例中的两条现有示例路径保持原文不变，真实 `D:/SoftWare/Qt/6.11.0/msvc2022_64` 和 `D:/SoftWare/Microsoft Visual Studio/18/Community` 只存在于被 `.gitignore` 排除的 `CMakeUserPresets.json`。

- [ ] **步骤 3：更新构建文档**

`docs/BUILD.md` 写清：

- clone 后运行 `git submodule update --init --recursive`；UI2 固定 ZzPureTools commit。
- `zzlogg_ui2` 要求 CMake 3.23+、Qt 6.8+（Core/Gui/Widgets/Svg/Concurrent/Test 和匹配版本 Qt private development files）、C++20 compiler。
- macOS UI2 的 deployment target 是 ZzPureTools 要求的 13.3；旧目标在 UI2 关闭时不受此值影响。
- `KLOGG_BUILD_UI2` 默认 OFF，旧目标仍是 Qt 6/C++17 路径。
- Ninja 跨平台命令：`cmake --workflow --preset ninja-ui2-debug`。
- Windows 用户命令：`cmake --workflow --preset windows-qt6-ui2-debug`。
- 第一阶段无 UI2 portable/zip/installer；绿色目录仍只对应 `klogg_portable_folder`。

- [ ] **步骤 4：用用户的 Windows 工具链执行完整验收**

编辑步骤 1 已复制且被忽略的 `CMakeUserPresets.json`，为 `windows-qt6-ui2` 增加 UI2 cache variables，并继续使用：

```text
CMAKE_GENERATOR_INSTANCE=D:/SoftWare/Microsoft Visual Studio/18/Community
CMAKE_PREFIX_PATH=D:/SoftWare/Qt/6.11.0/msvc2022_64
CMake=D:/SoftWare/CMake/bin/cmake.exe
```

运行：

```powershell
git submodule update --init --recursive
& 'D:/SoftWare/CMake/bin/cmake.exe' --workflow --preset windows-qt6-ui2-debug
& 'D:/SoftWare/CMake/bin/cmake.exe' --build --preset windows-qt6-debug `
  --target klogg klogg_portable
```

预期：UI2 全部测试 PASS；旧两个 GUI 目标继续构建。

- [ ] **步骤 5：执行 Windows 手动交互验收**

启动 Debug 和 RelWithDebInfo 的 `zzlogg_ui2.exe --multi --new-session`，逐项确认：

- 标题栏顺序是图标、File / Edit / View / Tools / Encoding / Favorites / Help 原菜单、全窗口绝对居中的标题、主题、置顶、系统按钮。
- 无文件标题为 `ZzLogg`；活动文件标题为 `文件名 — ZzLogg`；长标题右侧省略但视觉中心不偏移。
- 原文件信息工具栏、document tabs、主日志、搜索/过滤栏、过滤结果、splitter 和 status bar 均在原位置并可操作。
- 标题栏和 Settings 的 System/Light/Dark 相互同步，两个窗口实时同步；重启后保持选择。
- Windows 切换系统 Light/Dark 时 System 模式实时跟随；开启系统高对比度时高对比度优先。
- 置顶只影响当前窗口，重启不保存。
- 最小化、最大化/还原、关闭、拖动、双击标题栏、系统菜单、缩放 100%/125%/150%/200% 均正常。
- 普通宽度菜单横向展开，缩窄窗口后 Adaptive 模式折叠，重新拉宽后恢复；菜单弹出、Alt 快捷键和键盘导航仍使用原 QAction。
- 在不同 DPI 的多显示器之间移动窗口时标题栏和命中区域正确刷新。
- 新旧版本打开同一 fixture 时，滚动、选择、搜索、过滤和 follow 行为以及结果一致。
- Debug 日志没有 `Fluent window initialization failed`、`attach failed` 或 `configureChrome failed`；出现这些文本即视为 Windows 验收失败，不能接受 fallback 结果。

- [ ] **步骤 6：执行 Linux/macOS 编译门**

在安装 Qt 6.8+ 和对应 private development files 的平台运行：

```bash
git submodule update --init --recursive
cmake --preset ninja-ui2-debug
cmake --build --preset ninja-ui2-debug --target zzlogg_ui2 zzlogg_fluent_shell_test
```

预期：两个平台均完成编译和链接；第一阶段不要求 WindowKit 的平台交互验收。若有桌面 runner，再执行 `ctest --preset ninja-ui2-debug -R '^zzlogg_ui2\.'`。

- [ ] **步骤 7：执行最终回归和依赖边界检查**

运行：

```powershell
git diff --check
git submodule status --recursive
git ls-files --stage 3rdparty/vendor/ZzPureTools
& 'D:/SoftWare/CMake/bin/ctest.exe' --test-dir out/build/windows-qt6-ui2 `
  -C Debug -R '^zzlogg_ui2\.' --output-on-failure
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/windows-qt6-ui2 `
  --config Debug --target klogg klogg_portable zzlogg_ui2
& 'D:/SoftWare/CMake/bin/cmake.exe' --build out/build/windows-qt6-ui2 --config Debug
if (Test-Path 'out/build/windows-qt6-ui2/output/Debug/klogg_grep.exe') {
  throw 'klogg_grep unexpectedly entered the default build'
}
```

再用 `dumpbin /DEPENDENTS` 检查：`klogg.exe` 和 `klogg_portable.exe` 不依赖 `Zz*.dll`；`zzlogg_ui2.exe` 不依赖 `ZzAppCore.dll` 或 `ZzPureTools.dll`，其运行目录包含 `$<TARGET_RUNTIME_DLLS>` 复制出的 ZzCore、ZzWindowKit、ZzFluentFoundation、ZzFluentUI 和 Qt DLL。

预期：无 whitespace 错误，submodule 指向批准提交，UI2 测试全绿，旧目标无 Zz 依赖。

- [ ] **步骤 8：提交 presets 与文档**

```powershell
git add CMakePresets.json CMakeUserPresets.json.example docs/BUILD.md
git commit -m "docs: 补充 UI2 跨平台构建与验收"
```

- [ ] **步骤 9：确认分支可交付状态**

运行：

```powershell
git status --short --branch
git log --oneline --decorate -10
```

预期：工作树干净；提交顺序依次覆盖依赖、主题配置、设置入口、runner、Shell、Runtime、冒烟测试和构建文档，没有混入 `.superpowers/` 或 `CMakeUserPresets.json`。
