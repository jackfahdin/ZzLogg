# 浅色与深色二态主题切换实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 把 ZzLogg 的主题入口收敛为标题栏浅色/深色直接切换，并把旧的 System、缺失和非法配置在启动时一次性迁移为明确的 Light 或 Dark。

**架构：** `ZzLoggFluentShell` 只把标题栏按钮点击翻译为明确的下一主题意图；`ZzLoggUiRuntime` 是唯一执行与持久化主题变更的组件；`Configuration` 继续识别 `System`，但它只作为旧配置迁移哨兵。所有窗口共享一个 `ZzThemeController`，因此确认后的主题快照仍由现有机制同步。

**技术栈：** C++17、Qt 6 Widgets、Qt Test、ZzFluentUI、CMake/CTest

---

## 实施边界与文件职责

- `src/ui2/src/zzloggfluentshell.cpp`：启用 ZzPureTools 的 Toggle 交互、隐藏非二态菜单动作、把点击转换为相反主题请求。
- `src/ui2/include/zzloggfluentshell.h`：声明 Shell 内部的切换意图转换函数。
- `src/ui2/src/zzlogguiruntime.cpp`：迁移旧主题值，只接受并持久化明确的 Light/Dark。
- `src/ui/src/optionsdialog.cpp`：设置页仅展示 Light/Dark。
- `src/app/applicationrunner.cpp`：冒烟探针通过真实标题栏按钮验证二态切换。
- `tests/ui2/fluentshelltest.cpp`：验证 Toggle 模式、可见动作和按钮意图。
- `tests/ui2/runtimecontracttest.cpp`：验证启动迁移、非法请求拒绝、持久化与多窗口同步。
- `tests/ui2/optionsthemetest.cpp`：验证设置页二态选项与保存。
- `tests/ui2/themeconfigurationtest.cpp`：锁定旧配置解析哨兵，覆盖缺失值。

不修改 `3rdparty/vendor/ZzPureTools`；不删除 `UiThemeMode::System`，避免破坏旧配置读取兼容性。

## 工作目录与通用命令

所有步骤在独立工作树中执行：

```powershell
Set-Location 'D:\File\Program\GitCode\ZzLogg\.worktrees\two-state-theme-toggle'
```

现有构建目录为 `out/ui-vs`，配置为 `RelWithDebInfo`。每个任务先写失败测试，再做最小实现，并在任务末提交。

### 任务 1：把 System 收敛为启动迁移哨兵

**文件：**

- 修改：`tests/ui2/themeconfigurationtest.cpp`
- 修改：`tests/ui2/runtimecontracttest.cpp`
- 修改：`src/ui2/src/zzlogguiruntime.cpp`

- [ ] **步骤 1：补上旧配置与缺失配置的契约测试**

在 `ThemeConfigurationTest` 中新增缺失值测试，明确配置层仍返回迁移哨兵且不擅自猜测系统主题：

```cpp
void treatsMissingValueAsLegacySystemSentinel()
{
    QTemporaryDir dir;
    QVERIFY( dir.isValid() );
    QSettings settings( dir.filePath( QStringLiteral( "config.ini" ) ),
                        QSettings::IniFormat );
    QVERIFY( !settings.contains( QStringLiteral( "view.themeMode" ) ) );

    Configuration loaded;
    loaded.retrieveFromStorage( settings );
    QCOMPARE( loaded.uiThemeMode(), UiThemeMode::System );
}
```

保留现有非法值规范化为 `System` 的测试，并把测试命名或注释调整为“交给运行时迁移”，不要把配置解析层改成依赖 GUI/系统主题。

- [ ] **步骤 2：给运行时增加会失败的启动迁移测试**

在 `RuntimeContractTest` 的第一个测试槽新增 `migratesLegacySystemToExplicitMode()`：

```cpp
auto& configuration = Configuration::getSynced();
configuration.setUiThemeMode( UiThemeMode::System );
configuration.save();

QString error;
auto runtime = ZzLoggUiRuntime::create( app, &error );
QVERIFY2( runtime, qPrintable( error ) );
auto* style = qobject_cast<ZzFluentUI::ZzFluentStyle*>( app.style() );
QVERIFY( style );

const auto resolvedMode = style->themeSnapshot()->mode();
QVERIFY( resolvedMode == ZzFluentUI::ZzThemeMode::Light
         || resolvedMode == ZzFluentUI::ZzThemeMode::Dark );
const auto expected = resolvedMode == ZzFluentUI::ZzThemeMode::Dark
    ? UiThemeMode::Dark
    : UiThemeMode::Light;
QCOMPARE( Configuration::getSynced().uiThemeMode(), expected );
QCOMPARE( uiThemeModeStorageValue( expected ),
          expected == UiThemeMode::Dark ? QStringLiteral( "dark" )
                                        : QStringLiteral( "light" ) );
runtime.reset();
```

同时直接读取 `PersistentInfo::getSettings( app_settings{} )` 的 `view.themeMode`，断言磁盘上已经是 `light` 或 `dark`，而不是只检查内存对象。

- [ ] **步骤 3：运行目标测试，确认新运行时断言失败**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target zzlogg_theme_configuration_test zzlogg_runtime_contract_test --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^(zzlogg_ui2\.theme_configuration|zzlogg_ui2\.runtime_contract)$' --output-on-failure
```

预期：配置解析测试通过；运行时迁移测试因当前仍保存 `System` 而失败。

- [ ] **步骤 4：在运行时做一次性解析与持久化**

在 `zzlogguiruntime.cpp` 中加入只在启动阶段使用的辅助逻辑：

```cpp
UiThemeMode resolveExplicitThemeMode( UiThemeMode configuredMode,
                                      ZzFluentUI::ZzThemeController& theme )
{
    if ( configuredMode != UiThemeMode::System ) {
        return configuredMode;
    }

    theme.setMode( ZzFluentUI::ZzThemeMode::System );
    return theme.resolvedMode() == ZzFluentUI::ZzThemeMode::Dark
        ? UiThemeMode::Dark
        : UiThemeMode::Light;
}
```

`ZzLoggUiRuntime::create()` 创建控制器后调用该函数。仅当原配置是 `System` 时，立即更新 `Configuration` 并 `save()`；然后把控制器设置为解析后的明确模式。已有 Light/Dark 不重复写盘。

- [ ] **步骤 5：重新运行两项测试，确认通过**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target zzlogg_theme_configuration_test zzlogg_runtime_contract_test --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^(zzlogg_ui2\.theme_configuration|zzlogg_ui2\.runtime_contract)$' --output-on-failure
```

- [ ] **步骤 6：提交启动迁移**

```powershell
git add src/ui2/src/zzlogguiruntime.cpp tests/ui2/runtimecontracttest.cpp tests/ui2/themeconfigurationtest.cpp
git commit -m "feat: migrate legacy theme mode"
```

### 任务 2：让运行时只接受明确的浅色/深色请求

**文件：**

- 修改：`tests/ui2/runtimecontracttest.cpp`
- 修改：`src/ui2/src/zzlogguiruntime.cpp`

- [ ] **步骤 1：把非法主题请求写成失败测试**

扩展 `decoratesRealWindowsAndRoutesSemanticState()` 末尾的主题断言。先确保当前主题和配置为 Dark，然后分别发送 HighContrast、System 以及旧的 `MainWindow::uiThemeChanged( System )`：

```cpp
Q_EMIT secondShell->themeModeRequested( ZzFluentUI::ZzThemeMode::HighContrast );
QCOMPARE( Configuration::getSynced().uiThemeMode(), UiThemeMode::Dark );
QCOMPARE( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark );

Q_EMIT secondShell->themeModeRequested( ZzFluentUI::ZzThemeMode::System );
QCOMPARE( Configuration::getSynced().uiThemeMode(), UiThemeMode::Dark );
QCOMPARE( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark );

Q_EMIT first->uiThemeChanged( UiThemeMode::System );
QCOMPARE( style->themeSnapshot()->mode(), ZzFluentUI::ZzThemeMode::Dark );
```

- [ ] **步骤 2：运行测试，确认当前实现会接受非法请求**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target zzlogg_runtime_contract_test --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.runtime_contract$' --output-on-failure
```

预期：HighContrast 或 System 请求改变配置/快照，测试失败。

- [ ] **步骤 3：收紧请求映射和执行入口**

把 `fromZzThemeMode()` 改为返回 `std::optional<UiThemeMode>`，只映射 Light/Dark：

```cpp
std::optional<UiThemeMode> fromZzThemeMode( ZzFluentUI::ZzThemeMode mode )
{
    switch ( mode ) {
    case ZzFluentUI::ZzThemeMode::Light: return UiThemeMode::Light;
    case ZzFluentUI::ZzThemeMode::Dark: return UiThemeMode::Dark;
    case ZzFluentUI::ZzThemeMode::System:
    case ZzFluentUI::ZzThemeMode::HighContrast: return std::nullopt;
    }
    return std::nullopt;
}
```

Shell 信号连接仅在 optional 有值时调用 `applyTheme()`；`applyTheme()` 本身再防御性忽略 `UiThemeMode::System`，保证所有入口都不能恢复跟随系统。

- [ ] **步骤 4：运行运行时契约测试，确认通过**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target zzlogg_runtime_contract_test --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.runtime_contract$' --output-on-failure
```

- [ ] **步骤 5：提交运行时二态约束**

```powershell
git add src/ui2/src/zzlogguiruntime.cpp tests/ui2/runtimecontracttest.cpp
git commit -m "fix: reject non-binary theme requests"
```

### 任务 3：把标题栏主题按钮改为直接切换

**文件：**

- 修改：`tests/ui2/fluentshelltest.cpp`
- 修改：`src/ui2/include/zzloggfluentshell.h`
- 修改：`src/ui2/src/zzloggfluentshell.cpp`

- [ ] **步骤 1：把 Toggle 模式和可见菜单动作写成失败测试**

在 `verifySharedThemeObservationAndForwarding()` 中断言两个标题栏都使用 Toggle，并只保留 Light/Dark 动作可见：

```cpp
QCOMPARE( firstTitleBar->themeInteractionMode(),
          ZzFluentUI::ZzTitleBarThemeInteractionMode::Toggle );
QList<ZzFluentUI::ZzThemeMode> visibleModes;
for ( QAction* action : firstTitleBar->themeMenu()->actions() ) {
    if ( action->isVisible() ) {
        visibleModes.append(
            static_cast<ZzFluentUI::ZzThemeMode>( action->data().toInt() ) );
    }
}
QCOMPARE( visibleModes,
          QList<ZzFluentUI::ZzThemeMode>( { ZzFluentUI::ZzThemeMode::Light,
                                            ZzFluentUI::ZzThemeMode::Dark } ) );
```

查找 `zzTitleBarThemeButton`，点击时 Shell 应发出明确的相反主题，但不直接改变控制器：

```cpp
auto* button = firstTitleBar->findChild<QToolButton*>(
    QStringLiteral( "zzTitleBarThemeButton" ) );
QVERIFY( button );
QSignalSpy requestSpy( firstInstalled.value(),
                       &ZzLoggFluentShell::themeModeRequested );
QTest::mouseClick( button, Qt::LeftButton );
QCOMPARE( requestSpy.takeFirst().at( 0 ).value<ZzFluentUI::ZzThemeMode>(),
          ZzFluentUI::ZzThemeMode::Dark );
QCOMPARE( theme.mode(), ZzFluentUI::ZzThemeMode::Light );

theme.setMode( ZzFluentUI::ZzThemeMode::Dark );
QTest::mouseClick( button, Qt::LeftButton );
QCOMPARE( requestSpy.takeFirst().at( 0 ).value<ZzFluentUI::ZzThemeMode>(),
          ZzFluentUI::ZzThemeMode::Light );
```

- [ ] **步骤 2：运行 Shell 测试，确认默认 Menu 行为导致失败**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target zzlogg_fluent_shell_test --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.fluent_shell$' --output-on-failure
```

- [ ] **步骤 3：配置 ZzPureTools Toggle 并翻译切换意图**

创建标题栏后设置：

```cpp
titleBar->setThemeInteractionMode(
    ZzFluentUI::ZzTitleBarThemeInteractionMode::Toggle );
for ( QAction* action : titleBar->themeMenu()->actions() ) {
    const auto mode = static_cast<ZzFluentUI::ZzThemeMode>( action->data().toInt() );
    action->setVisible( mode == ZzFluentUI::ZzThemeMode::Light
                        || mode == ZzFluentUI::ZzThemeMode::Dark );
}
```

在 `ZzLoggFluentShell` 增加私有 `requestThemeToggle()`。它读取共享控制器的 `snapshot()->mode()`，当前为 Dark 时发 Light，否则发 Dark；不要在 Shell 中调用 `theme_->setMode()`。

连接框架信号：

```cpp
QObject::connect( retainedTitleBar,
                  &ZzFluentUI::ZzFluentTitleBar::themeToggleRequested,
                  retainedShell, &ZzLoggFluentShell::requestThemeToggle );
```

保留原 `themeModeRequested` 转发，以支持延迟弹出的 Light/Dark 菜单动作；隐藏动作即使被程序化触发，也由任务 2 的运行时守卫处理。

- [ ] **步骤 4：运行 Shell 测试，确认通过**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target zzlogg_fluent_shell_test --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.fluent_shell$' --output-on-failure
```

- [ ] **步骤 5：提交标题栏切换**

```powershell
git add src/ui2/include/zzloggfluentshell.h src/ui2/src/zzloggfluentshell.cpp tests/ui2/fluentshelltest.cpp
git commit -m "feat: toggle theme from title bar"
```

### 任务 4：把设置页收敛为浅色和深色

**文件：**

- 修改：`tests/ui2/optionsthemetest.cpp`
- 修改：`src/ui/src/optionsdialog.cpp`

- [ ] **步骤 1：先把二态选项写成失败测试**

把 `showsOnlyTheModeOwnedByTheEntryPoint()` 的 Fluent 断言改为：

```cpp
QCOMPARE( combo->count(), 2 );
QCOMPARE( combo->itemData( 0 ).toInt(), static_cast<int>( UiThemeMode::Light ) );
QCOMPARE( combo->itemData( 1 ).toInt(), static_cast<int>( UiThemeMode::Dark ) );
QCOMPARE( combo->findData( static_cast<int>( UiThemeMode::System ) ), -1 );
```

在保存测试中不要依赖固定下标，使用 `findData( Light )`。另加一条断言：构造对话框时若配置仍是旧 System，组合框安全回退到 Light。

- [ ] **步骤 2：运行设置页测试，确认三态 UI 导致失败**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target zzlogg_options_theme_test --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.options_theme$' --output-on-failure
```

- [ ] **步骤 3：删除用户可见的 System 选项**

在 `OptionsDialog` 构造函数中仅添加：

```cpp
themeModeComboBox->addItem( tr( "Light" ), static_cast<int>( UiThemeMode::Light ) );
themeModeComboBox->addItem( tr( "Dark" ), static_cast<int>( UiThemeMode::Dark ) );
```

保留现有 `findData()` 和找不到时回退索引 0 的逻辑；运行时正常启动后配置已经迁移，回退仅用于隔离测试和防御旧调用路径。

- [ ] **步骤 4：运行设置页测试，确认通过**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target zzlogg_options_theme_test --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.options_theme$' --output-on-failure
```

- [ ] **步骤 5：提交设置页二态选项**

```powershell
git add src/ui/src/optionsdialog.cpp tests/ui2/optionsthemetest.cpp
git commit -m "feat: limit settings to light and dark"
```

### 任务 5：让应用冒烟测试走真实按钮路径

**文件：**

- 修改：`src/app/applicationrunner.cpp`

- [ ] **步骤 1：先运行现有应用冒烟，观察旧探针失败点**

在前四个任务完成后运行：

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target klogg --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.application_smoke$' --output-on-failure
```

预期：旧探针仍要求 System 初始状态或依赖主题菜单，测试失败。

- [ ] **步骤 2：固定冒烟初态并点击真实主题按钮**

在 smoke 配置准备阶段把初始模式改为 Light：

```cpp
smokeConfiguration.setUiThemeMode( UiThemeMode::Light );
```

在 `WaitForSearch` 阶段删除查找 Dark 菜单动作的代码，改为查找并点击：

```cpp
auto* const themeButton = state->firstTitleBar->findChild<QToolButton*>(
    QStringLiteral( "zzTitleBarThemeButton" ) );
if ( themeButton == nullptr ) {
    finishUi2Smoke( *state, EXIT_FAILURE,
                    QStringLiteral( "theme toggle button is missing" ) );
    return;
}
if ( state->firstTitleBar->property( "themeMode" ).toInt()
         != static_cast<int>( UiThemeMode::Light )
     || state->secondTitleBar->property( "themeMode" ).toInt()
            != static_cast<int>( UiThemeMode::Light ) ) {
    finishUi2Smoke( *state, EXIT_FAILURE,
                    QStringLiteral( "theme did not begin in Light mode" ) );
    return;
}
themeButton->click();
```

保留 `WaitForTheme` 阶段对两个标题栏都变成 Dark、配置已持久化为 Dark 的断言。

- [ ] **步骤 3：重新构建并运行冒烟测试**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --target klogg --parallel
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.application_smoke$' --output-on-failure
```

- [ ] **步骤 4：提交冒烟探针更新**

```powershell
git add src/app/applicationrunner.cpp
git commit -m "test: exercise theme toggle in smoke probe"
```

### 任务 6：整体验证与分支收尾

**文件：**

- 检查：本计划列出的所有修改文件
- 检查：`docs/superpowers/specs/2026-09-03-two-state-theme-toggle-design.md`

- [ ] **步骤 1：构建完整 RelWithDebInfo 配置**

```powershell
D:\SoftWare\CMake\bin\cmake.exe --build out/ui-vs --config RelWithDebInfo --parallel
```

- [ ] **步骤 2：运行完整测试集**

```powershell
D:\SoftWare\CMake\bin\ctest.exe --test-dir out/ui-vs -C RelWithDebInfo --output-on-failure
```

期望：53/53（若实施期间新增 CTest 条目则以新总数为准）全部通过。

- [ ] **步骤 3：检查补丁质量和范围**

```powershell
git diff --check master...HEAD
git status --short
git log --oneline --decorate -8
```

确认：

- 用户界面没有 System/HighContrast 入口。
- 启动后 `Configuration::uiThemeMode()` 只稳定为 Light/Dark。
- 标题栏点击一次只发一个相反主题请求。
- 多窗口主题和磁盘配置同步。
- `3rdparty/vendor/ZzPureTools` 没有修改。
- 没有把 `out/`、临时子模块目录或用户未跟踪文件加入提交。

- [ ] **步骤 4：如有实现后文档偏差，先同步设计文档再提交**

仅在实现确实需要偏离批准设计时修改设计文档；禁止留下 TODO 或用文档掩盖测试缺口。

- [ ] **步骤 5：提交最终必要调整**

若步骤 3/4 产生必要修改，先用 `git status --short` 确认范围，只逐个暂存本功能文件，再使用提交信息 `docs: align two-state theme behavior` 提交；如果没有必要修改，则不创建空的收尾提交。

最后保持 `codex/two-state-theme-toggle` 为线性提交序列，等待按快进方式合并到 `master`；未经明确要求不要推送。
