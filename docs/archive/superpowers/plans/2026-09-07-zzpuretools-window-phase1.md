# ZzPureTools 主窗口直接接入实现计划（阶段一）

> **面向执行者：** 使用 `executing-plans` 逐任务实施，并用复选框记录进度。每任务完成相关验证后继续；本计划不要求另外创建任务或委派代理。

**目标：** 主窗口在创建时直接拥有 ZzPureTools 标题栏和菜单，继续使用现有日志页面；完成后移除 `src/ui2`。

**架构：** Qt `QMainWindow` 为宿主，应用运行时传入共享主题控制器，窗口组合 `WindowChrome`。菜单直接创建在框架提供的容器中，文件与搜索业务沿用现有窗口接口。

**技术栈：** C++20、Qt 6 Widgets、ZzPureTools、CMake、QtTest；本机 Qt 6.11/MSVC 2026。

## 1. 基线和实施范围

关联 [总体路线](2026-09-07-zzpuretools-ui-roadmap.md)。基于 `master` 的 `248fc7c5` 和已更新的 ZzPureTools `d6b90f1` 实施，不合并 `main`。

此次不拆 `CrawlerWidget`，不更换文件标签控件，不改变设置格式和编码算法。保持菜单、工具栏内容和屏幕布局，复用当前窗口公开接口，避免让 IPC、多窗口、存储启动和测试同时重写。

实施前记录 `git status --short`，保留当前未提交文件。若进入隔离 worktree，必须单独带入已选中的子仓库引用，不能因从 HEAD 创建而退回旧版 ZzPureTools。文档规划本身在当前工作区完成，不创建多余开发分支。

## 2. 文件与依赖设计

新增文件按现有 `include` / `src` 约定组织：

| 新增文件 | 职责 |
| --- | --- |
| `src/ui/include/windowchrome.h`、`src/ui/src/windowchrome.cpp` | 标题栏、窗口代理、原生回退和状态同步 |
| `src/ui/include/uithemecontext.h` | 小型窗口构造上下文，传递主题控制器引用 |
| `src/ui/include/uiruntime.h`、`src/ui/src/uiruntime.cpp` | 从旧运行时迁移全局主题创建和窗口工厂注册 |
| `src/ui/src/mainwindowmenus.cpp` | `MainWindow::createMenus()` 实现，接收明确菜单容器 |
| `src/ui/src/mainwindowtoolbar.cpp` | `MainWindow::createToolBars()` 实现，保留动作和状态栏成员 |
| `tests/ui2/windowchrometest.cpp` | 新外观组件的状态、失败和生命周期测试 |

`createActions()` 暂时保留在 `mainwindow.cpp`，避免第一阶段引入全功能动作注册框架。菜单和工具栏源文件拆分不改变 `MainWindow` 头文件中现有业务动作的所有权。

保留 `src/ui2` 到任务 5 的验证通过，再统一删除。最终构建依赖：

```text
klogg（应用）
  → zzlogg_ui_runtime（src/ui 定义；可依赖 KloggApp 和 klogg_ui）
  → klogg_ui（业务 UI + WindowChrome）
      → Zz::FluentUI / Zz::FluentFoundation / Zz::WindowKit
      → 原有日志、配置、搜索依赖
```

运行时是独立构建目标，不能让 `klogg_ui` 反向依赖运行时，否则会产生循环。应用侧名字从 `ui2` 迁出；测试目录和兼容 preset 在阶段一保留，避免同时破坏现有 CI 命令。

## 3. 新接口约定

以下是拟新增的应用接口，不是宣称 ZzPureTools 已有这些类：

```cpp
struct UiThemeContext {
    ZzFluentUI::ZzThemeController& controller;
};

// 保留原构造函数供现有测试和原生回退场景使用。
explicit MainWindow(WindowSession session);
MainWindow(WindowSession session, UiThemeContext context);

// 在 KloggApp 中以工厂替换创建后的装饰回调。
using MainWindowFactory = std::function<MainWindow*(WindowSession)>;
void setMainWindowFactory(MainWindowFactory factory);

// 新 WindowChrome 的关键接口。
WindowChrome(QMainWindow& window, UiThemeContext context);
QMenuBar& commandMenuBar() const;
bool usesNativeFallback() const;
void setDocumentName(const QString& documentName);
```

`WindowChrome` 定义为 QObject 组件，由 `MainWindow` 通过 `std::unique_ptr` 管理，其自身不再设置 QObject 父对象。标题栏交由窗口拥有，代理由组件拥有；窗口析构期间先释放组件和代理，再释放 Qt 子控件。

`commandMenuBar()` 始终返回有效容器：成功接入返回 `ZzFluentTitleBar::menuBar()`；平台代理失败返回宿主原生菜单栏。两种情况下都在业务菜单创建之前选定容器。

单参数窗口构造路径继续用原生容器，供原有非外观测试使用。生产应用必须通过工厂创建带主题上下文的窗口，并由启动测试断言框架标题栏存在，防止意外走回原生测试路径。

## 4. 任务 1：冻结窗口行为基线

**文件：** 修改 `tests/ui2/applicationtranslationtest.cpp`、`tests/ui2/documenttabclosetest.cpp`；参考 `fluentshelltest.cpp` 和 `titlebarlifetimesmoke.cmake`。

- [ ] 记录现有窗口在浅色、深色、中英文、正常宽度和窄窗口下的布局，作为人工对照证据。
- [x] 运行现有行为测试，记录失败项，不把原有问题归因于重构。
- [x] 为菜单访问引入一个测试辅助函数：优先查找 `zzloggFluentTitleBar`，再取其 `menuBar()`；原生测试窗口则使用 `window.menuBar()`。
- [x] 保持动作名称、快捷键、选中状态、最后一个文件关闭以及语言切换断言。

迁移时保留如下行为测试（QtTest 片段，`window` 为测试创建的窗口）：

```cpp
auto* encoding = window.findChild<QMenu*>("encodingMenu");
QVERIFY(encoding);
QCOMPARE(encoding->windowType(), Qt::Popup);
auto* autoAction = encoding->findChild<QAction*>("encodingAutoAction");
QVERIFY(autoAction);
QVERIFY(encoding->actions().contains(autoAction));
```

基线命令：

```powershell
ctest --test-dir out/ui-vs -C Release --output-on-failure -R 'zzlogg_ui2\.(application_translation|document_tab_close|fluent_shell|options_theme|theme_configuration|application_smoke|titlebar_lifetime_smoke)$'
```

验收：记录明确的通过/失败结果；这些操作的语义在新实现中必须保持。

## 5. 任务 2：实现直接创建的 WindowChrome

**文件：** 创建 `windowchrome.h/.cpp`、`uithemecontext.h`、`windowchrometest.cpp`；修改 `src/ui/CMakeLists.txt`、`tests/ui2/CMakeLists.txt`。

- [x] 新建 `zzlogg_ui2.window_chrome` 测试，断言创建组件时菜单栏为空且容器由框架标题栏拥有。
- [x] 注入可失败的代理配置函数，覆盖附着失败、配置失败、成功三条路径；实现前运行测试确认缺少功能。
- [x] 复用原外观代码中的能力判断、置顶、图标、标题同步和按钮意图连接，去掉菜单迁移算法。
- [x] 成功时先配置代理，再由 `setMenuWidget()` 接管标题栏；返回标题栏菜单容器。
- [x] 失败时释放代理和临时标题栏，恢复窗口 flags，选择原生菜单容器；记录错误，窗口仍能操作和关闭。
- [x] 将标题格式化函数移入新组件文件，保留 Unicode 和长文件名测试。
- [x] 测试重复开关窗口、关闭事件被拒绝、最大化/还原、主题切换，以及销毁后延迟事件不访问旧指针。

实际框架 API 组合示例：

```cpp
auto titleBar = std::make_unique<ZzFluentUI::ZzFluentTitleBar>(&window);
titleBar->setMenuDisplayMode(ZzFluentUI::ZzTitleBarMenuDisplayMode::Adaptive);
titleBar->setThemeInteractionMode(
    ZzFluentUI::ZzTitleBarThemeInteractionMode::Toggle);
// 代理 attach/configureChrome 成功后提交标题栏所有权。
QMenuBar* menu = titleBar->menuBar();
window.setMenuWidget(titleBar.release());
Q_ASSERT(menu != nullptr);
```

菜单、主题按钮和置顶按钮应进入框架命中测试区域；系统按钮依平台能力配置。多点缩放和 Windows 拖动/吸附还需实际窗口检查，不能只依赖离屏测试。

## 6. 任务 3：菜单和工具栏直接组合

**文件：** 修改 `mainwindow.h/.cpp`；创建 `mainwindowmenus.cpp`、`mainwindowtoolbar.cpp`；修改 `src/ui/CMakeLists.txt`。

- [x] 增加带 `UiThemeContext` 的构造路径，并抽取共用初始化方法，避免两套构造函数复制业务初始化。
- [x] 在创建动作、菜单之前创建 `WindowChrome` 并确定菜单容器。
- [x] 将 `createMenus()` 改为 `createMenus(QMenuBar& menuBar)`，完整迁移现有菜单项，所有父对象从该容器取得。
- [x] 审计 `mainwindow.cpp` 中每个 `menuBar()` 调用；框架窗口不允许通过基类方法创建新的默认菜单栏。
- [x] 把菜单与工具栏构造实现移到对应文件，动作仍共享原有 `QAction`；状态字段仍由 `MainWindow` 管理。
- [x] 用真实生产构造路径验证编码菜单弹出、快捷键、工具栏选中状态、最近文件和收藏动态更新。

目标调用顺序：

```cpp
createActions();
createMenus(chrome_->commandMenuBar());
createToolBars();
// 接入原有文件标签、CrawlerWidget、QuickFindWidget 和会话信号。
```

验收：一套菜单、一套动作，不存在 `originalMenuBar_` 或迁移操作；打开、重新加载、跟随、收藏及关闭行为保持。

## 7. 任务 4：主题服务与窗口工厂

**文件：** 创建 `uiruntime.h/.cpp`；修改 `kloggapp.h`、`main.cpp`、`applicationrunner.cpp`、`applicationrunner.h`（若类型声明依赖旧运行时）、`mainwindow.h/.cpp`、相关 CMake。

- [x] 实现 `setMainWindowFactory()`，在 `newWindow()` 中选择已注册工厂或原生测试构造，保持原有窗口注册和信号连接顺序。
- [x] 从旧运行时迁入主题初始化和旧 `System` 配置迁移逻辑，注册创建带上下文窗口的工厂。
- [x] 主题服务创建完成后才允许新建生产窗口；退出时关闭窗口、清除工厂、释放样式对主题的引用，最后释放主题服务。
- [x] 标题栏和设置仅请求主题变更，运行时统一持久化与应用；保持多窗口同步，避免两个控制器各自写配置。
- [x] 沿用 `AbstractLogView::changeEvent()` 缓存刷新与工具栏图标重载，验证主日志、结果区、选区和禁用动作在深浅色下可读。
- [x] 改写 `runtimecontracttest.cpp` 的内部访问，覆盖新窗口和会话恢复窗口都经过工厂。

工厂实现形态：

```cpp
app.setMainWindowFactory([this](WindowSession session) {
    return new MainWindow(std::move(session), UiThemeContext{*theme_});
});
```

验收：一个主题控制器，现有窗口与之后创建的窗口主题一致；没有创建后装饰或二次菜单构建。

## 8. 任务 5：清理过渡层并形成唯一生产路径

**文件：** 修改 `src/CMakeLists.txt`、`src/ui/CMakeLists.txt`、`src/app/CMakeLists.txt`、`tests/ui2/CMakeLists.txt`、`cmake/ZzPureTools.cmake` 和引用旧类型的测试；删除 `src/ui2/`；更新 `docs/UI_CODE_GUIDE.md`。

- [x] 在 `src/ui/CMakeLists.txt` 定义独立 `zzlogg_ui_runtime` 目标，将应用链接从 `zzlogg_ui2_runtime` 替换为新目标。
- [x] 移除 `src/CMakeLists.txt` 中 `ui2` 子目录注册；保留测试目录与 preset 的兼容名字。
- [x] 将 `klogg_copy_ui2_runtime_dlls` 改为中性名称并更新全部调用者，保持实际 DLL 复制集合不变。
- [x] 迁移旧 shell 中仍有效的行为测试；删除只验证已取消的菜单迁移/事务回滚的测试，新组件失败测试替代它们。
- [x] 审计旧运行时、私有头文件、友元测试和诊断属性引用，确保不会留下新旧双路径。
- [x] 无调用者后删除旧源目录，更新学习指南与应用启动冒烟测试的结构断言。

检查命令：

```powershell
rg -n 'ZzLoggFluentShell|zzloggfluentshell|zzlogg_ui2_shell|zzlogg_ui2_runtime|setWindowDecorator|commitFluentMenu|originalMenuBar_' src tests cmake
git diff --check
```

验收：生产代码不再引用上述旧接入层标识。测试名里的 `ui2` 可以暂时保留，不将单纯改名作为完成目标。

## 9. 任务 6：构建、行为与运行目录验收

- [x] 使用本机现有工具链配置并构建 Release，新测试目标与应用一起构建。
- [x] 执行阶段一基线测试、新 `window_chrome` 测试及完整已启用测试集；逐项处理新增失败。
- [x] 为自动测试使用独立配置目录和现有 smoke 隔离机制，不让测试写用户设置或真实会话。
- [x] 生成带依赖的运行目录；调用会清理目录的打包目标前核对目标内没有用户数据，必要时选择新构建输出目录。
- [ ] 在真实 Windows 窗口检查标题居中、菜单弹出/键盘操作、窄窗口自适应、拖动缩放、系统按钮、深浅色与中英文切换。
- [x] 打开两个日志并搜索，关闭最后一个文件后再次打开，验证多窗口、退出和会话恢复。
- [ ] 对同一代表性大日志比较前后打开和搜索耗时、峰值内存；不因纯 UI 变化引入全文加载。

本机参考命令（在主工作区执行；隔离工作区使用自己的构建路径）：

```powershell
cmake -S . -B out/ui-vs '-DCMAKE_PREFIX_PATH=D:/SoftWare/Qt/6.11.0/msvc2022_64' -DKLOGG_BUILD_UI_TESTS=ON
cmake --build out/ui-vs --config Release --parallel 8
ctest --test-dir out/ui-vs -C Release --output-on-failure
cmake --build out/ui-vs --config Release --target zzlogg_runtime_folder
```

最终提供 `out/ui-vs/runtime/Release/ZzLogg-runtime/ZzLogg.exe` 或实际隔离输出路径，并报告测试通过数、原有失败与新失败、人工检查结果。没有 Linux/macOS 运行环境时明确标为未验证，不宣称跨平台验收完成。

## 10. 阶段完成条件

- [x] 生产窗口直接创建框架标题栏与菜单，布局和核心交互符合原界面。
- [x] `src/ui2` 已删除，应用 UI 位于 `src/ui`，运行时与业务 UI 构建依赖无环。
- [x] 原日志页面可用，主题、语言、窗口销毁与会话回归通过。
- [x] 提供可运行预览和更新后的学习文档。
- [x] 未经要求不推送；提交时只包含阶段相关文件，不捆入用户图片或其他任务改动。

## 11. 执行记录

### 2026-09-07：隔离工作区与基线崩溃排查

- 已从计划提交 `f66e9d12` 创建 `codex/zzpuretools-ui-refactor`，目录为 `.worktrees/zzpuretools-ui-refactor`；子仓库使用指定版本 `d6b90f1`。原工作区未提交内容保持不变。
- 现有构建的 7 项基线中，6 项通过，`application_translation` 在处理延迟事件时发生访问冲突。用户确认先修复此问题再重构。
- 独立工作区重新配置、编译后，新增的标签页销毁和主窗口销毁回归均复现 `QObject::parent → QStyleOption::initFrom` 崩溃，排除仅由旧构建产物导致的可能。
- 根因：图标/样式刷新用 `dispatchToMainThread` 投递到全局事件分发器，捕获的窗口指针在窗口销毁后仍可能被使用。改用已有的 `dispatchToObject(callback, this)`，保持异步执行，并让窗口销毁自动取消待处理回调。
- 修复后 `application_translation`、`document_tab_close` 各连续运行 5 次通过；另一次包含 `options_theme`、`theme_configuration` 的 4 项检查全部通过。独立代码审查无阻塞问题。
- 独立工作区完整 Release 构建通过；完整 CTest **56/56 通过**（39.37 秒），包括框架外观、应用启动、会话恢复及异步对象/标题栏生命周期测试。此处 Linux 相关用例是构建规则检查，不代表已在 Linux 实机运行。
- 此记录仅表示基线问题的修复进度，不代表 WindowChrome 接入、`src/ui2` 删除或阶段一整体完成。

### 2026-09-07：阶段一代码与自动验收交付

- 任务 2 至任务 5 的实现已完成：`WindowChrome` 在构造期确定菜单容器，`MainWindow` 直接组合菜单和工具栏，`UiRuntime` 统一主题与窗口工厂；删除旧 `src/ui2` 及其专用迁移测试。保留 `tests/ui2` 与 preset 名称以兼容已有命令。
- 生命周期审查发现关闭窗口从活动列表移除后可能未释放，已增加独立的受管窗口列表、关闭后的延迟销毁及运行时退出时的确定性释放，并清除样式启用标志。新增测试先复现失败，修复后通过，定向复审无阻塞问题。
- 最终完整 CTest **57/57 通过，34.95 秒**；Release 全量构建及后续新增测试目标构建通过。运行目录生成、目录依赖验证、双文件启动、最后标签关闭、会话恢复、主题、翻译和对象生命周期用例均通过。
- Windows 原生渲染用例生成深浅色 × 中英文 × 宽窄窗口共 8 张截图，已查看浅色中文宽窗口和深色英文窄窗口：标题居中、日志区域随主题变化、窄窗口菜单折叠。截图位于 `out/ui-vs/tests/ui2/ui-captures/windows/`；离屏截图独立位于 `offscreen/`，其字体方框不能作为原生显示证据。800 像素为请求宽度，英文控件最小宽度可能将实际窗口撑至 857 像素。
- 大日志回归使用 138,420,224 字节、2,097,152 行的合成日志，正确得到 2,048 条 ERROR 结果；Windows 原生单次打开 284 ms、搜索 184 ms。外部采样的测试进程峰值工作集为 342,491,136 字节（约 327 MiB），包含测试和 Qt 开销；本次启用的搜索后端未使用 Hyperscan/VectorScan。
- **未完成的验收项如实保留未勾选：** 实施前的完整截图基线未留存；Windows 手工拖动、吸附、多屏 DPI 尚未验证；未进行相同环境的旧版/新版大日志性能对照，不能宣称性能无退化。当前截图和大日志回归不能替代这些证据。Linux/macOS 未做实机验证。
- 可运行程序：`.worktrees/zzpuretools-ui-refactor/out/ui-vs/runtime/Release/ZzLogg-runtime/ZzLogg.exe`（相对主仓库根目录）。运行时依赖已置于同一运行目录及必要子目录，不生成 ZIP。
- 学习文档已按启动、运行时、主窗口、标题栏、菜单工具栏、日志搜索、设置、主题翻译与测试分类更新到 `docs/UI_CODE_GUIDE.md`。
- 按用户要求，每个已验证逻辑改动单独提交，使用中文标题和中文详细说明；分支保留为 `codex/zzpuretools-ui-refactor`，未合并、未推送。原 master 工作区中的图片、学习文档及其他未提交文件未改动。阶段二至五仍是后续路线，不属于此份阶段一文件级实施计划。
