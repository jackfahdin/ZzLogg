# ZzLogg UI 代码学习指南

本文对应 `codex/zzpuretools-ui-refactor` 的阶段一至四实现。界面仍使用 Qt 6 Widgets，日志显示与搜索逻辑沿用原实现；主窗口直接组合 ZzPureTools 标题栏、菜单和共享主题，文件标签生命周期由 DocumentWorkspace 管理，不再经过 `src/ui2` 装饰层。

## 1. 建议阅读顺序

1. [main.cpp](../src/app/main.cpp)：准备窗口框架并注入 UI 运行时。
2. [applicationrunner.cpp](../src/app/applicationrunner.cpp)：数据目录、语言、会话和事件循环。
3. [uiruntime.cpp](../src/ui/src/uiruntime.cpp)：创建共享主题、安装样式、注册窗口工厂。
4. [mainwindow.cpp](../src/ui/src/mainwindow.cpp)：窗口构造与文件/搜索业务协调。
5. [windowchrome.cpp](../src/ui/src/windowchrome.cpp)：标题栏、窗口按钮、原生回退。
6. [mainwindowmenus.cpp](../src/ui/src/mainwindowmenus.cpp)、[mainwindowtoolbar.cpp](../src/ui/src/mainwindowtoolbar.cpp)：菜单与工具栏。
7. [documentworkspace.cpp](../src/ui/src/documentworkspace.cpp)：文件标签、当前文档路由、有序会话保存恢复。
8. [crawlerwidget.cpp](../src/ui/src/crawlerwidget.cpp)：每份日志的主视图、过滤视图与搜索交互。

## 2. 目录与分层

| 位置 | 职责 |
| --- | --- |
| [src/app](../src/app) | 启动、IPC、多窗口、会话恢复、退出 |
| [src/ui/include](../src/ui/include) | UI 类接口 |
| [src/ui/src](../src/ui/src) | UI 组件与交互实现 |
| [src/ui/include](../src/ui/include) 中的 `.ui` 文件 | Qt Designer 的表单（与头文件同目录） |
| [src/settings](../src/settings) | 应用配置与持久化 |
| [src/logdata](../src/logdata) | 日志读取、索引、过滤数据，不属于界面外观 |
| [ZzPureTools](../3rdparty/vendor/ZzPureTools) | 上游标题栏、样式、主题与窗口平台适配 |
| [tests/ui2](../tests/ui2) | 保留兼容目录名的 UI 测试，不是第二套生产 UI |

构建中 `klogg_ui` 包含窗口和业务 UI，`zzlogg_ui_runtime` 是依赖它的独立目标。运行时不能反向放进 `klogg_ui`，否则会与应用窗口工厂形成依赖环。

## 3. 应用入口与窗口工厂

[KloggApp](../src/app/kloggapp.h) 管理窗口列表、激活状态和会话。

- `setMainWindowFactory()` 注册生产窗口的创建方法。
- 新窗口与会话恢复都通过同一条 `newWindow()` 路径。
- `UiRuntime::createWindow()` 将共享的 `UiThemeContext` 传入主窗口。
- 未注册工厂时保留原生窗口构造，供独立测试和启动回退使用。
- 事件循环结束时先销毁窗口，再释放样式和共享主题。

修改启动顺序前，先看存储与会话测试；不要让窗口创建提前到数据目录确定之前。

## 4. 主窗口与布局

[mainwindow.h](../src/ui/include/mainwindow.h) 是主窗口接口。
主窗口仍继承 `QMainWindow`，不是新造一套窗口基类。

构造顺序：选择标题栏/菜单容器 → 创建动作 → 创建菜单 → 创建工具栏 → 接入文件标签和快速查找。

- 文件工作区：[documentworkspace.cpp](../src/ui/src/documentworkspace.cpp)，继承 [TabbedCrawlerWidget](../src/ui/src/tabbedcrawlerwidget.cpp)，沿用原标签样式、拖动和菜单。
- 中央日志页面：[crawlerwidget.cpp](../src/ui/src/crawlerwidget.cpp)。
- 快速查找：[quickfindwidget.cpp](../src/ui/src/quickfindwidget.cpp)、[quickfindmux.cpp](../src/ui/src/quickfindmux.cpp)。
- 暂存器：[tabbedscratchpad.cpp](../src/ui/src/tabbedscratchpad.cpp)。

本阶段不增加导航栏、不改变主/过滤视图的排列，也不拆换日志渲染器。

### 4.1 文件工作区的责任边界

[documentworkspace.h](../src/ui/include/documentworkspace.h) 提供文档级入口：

- `openDocument()` 创建页面、应用旧视图上下文并选中新标签。
- `closeDocument()` 停止加载、移除标签、注销会话并延迟释放页面；无效索引不操作。
- `restoreDocuments()` 按现有会话顺序恢复文件，沿用原先的活动页选择规则。
- `saveDocuments()` 按当前标签顺序保存 ViewContext 和窗口几何信息，不改变配置格式。
- `currentDocumentChanged()` 在动作和快速查找路由更新之后通知 MainWindow；空工作区传入空指针，宿主清空标题、状态并禁用编辑菜单。
- `refreshQuickFindSelector()` 在过滤结果页变化后重新绑定快速查找。

工作区借用 `WindowSession`、`SignalMux` 和 `QuickFindMux`，所以 MainWindow 将工作区成员声明在它们之后，保证先销毁工作区。直接析构时也注销剩余文档，不留下已释放页面的会话登记；这个清理不覆盖已保存的恢复列表。

打开对话框、压缩包处理、最近文件/收藏、跨窗口激活、退出策略仍在 MainWindow。要学习“关闭一个文件”的流程，按 `MainWindow::closeTab()` → `DocumentWorkspace::closeDocument()` → `WindowSession::close()` 阅读。

## 5. 标题栏与菜单

[WindowChrome](../src/ui/include/windowchrome.h) 管理标题栏、平台代理和状态同步。

- 成功时，`commandMenuBar()` 返回 `ZzFluentTitleBar::menuBar()`。
- 附着、配置失败或抛异常时，恢复窗口 flags，使用原生菜单栏。
- 标题栏发出最小化、最大化、关闭、置顶和主题请求，由应用执行。
- 标题随活动文档更新，图标随窗口图标变化同步。

**重要：框架窗口不要调用 `QMainWindow::menuBar()` 来找菜单。** Qt 可能创建新的原生菜单栏并替换现有标题栏。使用 `windowChrome()->commandMenuBar()`，或通过标题栏的 `menuBar()` 获取。

[mainwindowmenus.cpp](../src/ui/src/mainwindowmenus.cpp) 只组合菜单；动作与业务槽仍由主窗口拥有。编码子菜单来自 [encodings.h](../src/ui/include/encodings.h)，必须保留 `Qt::Popup` 窗口类型。

## 6. 工具栏和状态信息

[mainwindowtoolbar.cpp](../src/ui/src/mainwindowtoolbar.cpp) 创建打开、重新加载、跟随、收藏按钮，以及路径、大小、日期、编码、行列信息。

- 按钮使用现有共享 QAction，菜单与工具栏不会各自维护一份状态。
- 图标加载：[iconloader.cpp](../src/ui/src/iconloader.cpp)。
- 路径信息：[pathline.cpp](../src/ui/src/pathline.cpp)。
- 搜索/加载提示：[infoline.cpp](../src/ui/src/infoline.cpp)。

主题刷新使用绑定对象的延迟调用。不要向全局事件队列投递捕获裸窗口指针的回调，否则窗口销毁后仍可能访问悬空指针。

## 7. 日志、搜索和绘制

[CrawlerWidget](../src/ui/include/crawlerwidget.h) 协调一份日志的读取、搜索状态和视图。

阶段三将页面构造分为三层，阅读时建议按这个顺序：

1. [LogPage](../src/ui/src/logpage.cpp)：垂直分割、主视图与底部搜索/结果区域的布局，不负责数据读取。
2. [SearchPanel](../src/ui/src/searchpanel.cpp)：搜索输入、选项、历史菜单、信息行和图标；只发出用户操作信号，不执行过滤任务。翻译保留原 `CrawlerWidget` 上下文。
3. [CrawlerWidget::setup](../src/ui/src/crawlerwidget.cpp)：组合页面并连接面板意图；搜索状态机、结果数据映射、自动刷新、快速查找与快捷键仍在此处。

调整控件排列或文案优先看前两层；调整搜索规则则继续追踪 CrawlerWidget 到 LogFilteredData，避免在 SearchPanel 中加入数据任务。

| 文件 | 学习重点 |
| --- | --- |
| [abstractlogview.cpp](../src/ui/src/abstractlogview.cpp) | 自绘文本、选择、滚动、调色板与缓存刷新 |
| [logmainview.cpp](../src/ui/src/logmainview.cpp) | 主日志视图 |
| [filteredview.cpp](../src/ui/src/filteredview.cpp) | 搜索结果/过滤视图 |
| [overviewwidget.cpp](../src/ui/src/overviewwidget.cpp) | 概览组件 |
| [predefinedfilterscombobox.cpp](../src/ui/src/predefinedfilterscombobox.cpp) | 预定义过滤条件选择 |
| [quickfind.cpp](../src/ui/src/quickfind.cpp) | 快速查找逻辑 |

外观重构时保留这些组件的数据接口，避免把大日志整体复制进普通文本编辑控件。

## 8. 深浅色主题

[UiRuntime](../src/ui/include/uiruntime.h) 持有唯一的 `ZzThemeController`，安装 `ZzFluentStyle`。

标题栏只发出请求，运行时负责应用与持久化；设置页发出的 `uiThemeChanged` 只触发应用，不重复写配置。界面只提供浅色和深色，旧 System 配置在启动时解析为显式模式。

日志自绘区域还需在 `changeEvent()` 中刷新颜色与缓存，不能以为更换 QApplication 样式就足够。

## 9. 设置和多语言

- [optionsdialog.cpp](../src/ui/src/optionsdialog.cpp)：设置按钮事务、配置保存、存储迁移及快捷键校验。
- [optionsdialogpages.cpp](../src/ui/src/optionsdialogpages.cpp)：设置页初始化、配置回填、字体/颜色展示与动态翻译；仍为同一个 OptionsDialog，不新增保存入口。
- [storagelocationpage.cpp](../src/ui/src/storagelocationpage.cpp)：数据目录选择。
- [storagebootstrapdialog.cpp](../src/ui/src/storagebootstrapdialog.cpp)：首次启动引导。
- [mainwindowtext.cpp](../src/ui/src/mainwindowtext.cpp)：菜单/动作文案上下文。
- [src/app/i18n](../src/app/i18n)：翻译目录。

动态切换语言依靠 `LanguageChange` 与 `reTranslateUI()` 等入口更新已存在的对象，不重新创建菜单或日志页面，以保留选择、搜索与编码状态。

## 10. 高亮、过滤与草稿窗口

- [highlightersdialog.cpp](../src/ui/src/highlightersdialog.cpp)：高亮集合及快速高亮设置；编辑副本，Apply/OK 时保存。
- [highlightersetedit.cpp](../src/ui/src/highlightersetedit.cpp)：单个高亮集合的名称和规则列表。
- [highlighteredit.cpp](../src/ui/src/highlighteredit.cpp)：单条规则、正则选项和自定义颜色。
- [predefinedfiltersdialog.cpp](../src/ui/src/predefinedfiltersdialog.cpp)：预定义过滤表格和保存事务。
- [scratchpad.cpp](../src/ui/src/scratchpad.cpp)：正文编辑器、转换动作及时间/进制结果展示。
- [tabbedscratchpad.cpp](../src/ui/src/tabbedscratchpad.cpp)：草稿标签、快捷键、稳定编号与关闭释放。

这些窗口通过 `changeEvent(LanguageChange)` 原地更新静态文案，不重新加载用户草稿。带图标的高亮/过滤窗口在样式或调色板变化后调用 `loadIcons()`；排队回调使用 `dispatchToObject(..., this)`，窗口销毁后不再执行。自定义高亮颜色不随全局主题重置。

草稿动作由工具栏拥有；关闭按钮和快捷键统一调用 `closeTab()`，从标签中移除后 `deleteLater()`，加号说明页始终保留。修改语言文案不要调用 `setPlainText()`，否则会破坏正文和撤销栈。

## 11. 测试与常见修改入口

- [windowchrometest.cpp](../tests/ui2/windowchrometest.cpp)：直接菜单容器、失败回退、异常与销毁。
- [windowchromebehaviortest.cpp](../tests/ui2/windowchromebehaviortest.cpp)：窗口按钮、图标、置顶与标题同步。
- [applicationtranslationtest.cpp](../tests/ui2/applicationtranslationtest.cpp)：真实框架窗口的语言切换、编码弹出菜单、搜索状态。
- [runtimecontracttest.cpp](../tests/ui2/runtimecontracttest.cpp)：工厂、多窗口主题、日志颜色及运行时销毁。
- [documenttabclosetest.cpp](../tests/ui2/documenttabclosetest.cpp)：最后一个文件关闭和延迟刷新生命周期。
- [documentworkspacetest.cpp](../tests/ui2/documentworkspacetest.cpp)：工作区关闭重开、非空析构、当前页动作路由、拖动排序与视图上下文恢复。

添加菜单项先改主窗口动作，再放入菜单/工具栏；修改主题入口先看 UiRuntime；改日志字体、选择或绘制先看 AbstractLogView；改窗口标题和系统按钮先看 WindowChrome。

阶段一的具体范围与验证记录见 [实施计划](superpowers/plans/2026-09-07-zzpuretools-window-phase1.md)。
阶段二的具体范围与验证记录见 [文件工作区计划](superpowers/plans/2026-09-07-document-workspace-phase2.md)。
阶段三见 [日志页面与搜索面板计划](superpowers/plans/2026-09-07-log-page-search-panel-phase3.md)。
阶段四见 [设置与辅助窗口计划](superpowers/plans/2026-09-07-settings-auxiliary-phase4.md)；应用集成测试覆盖设置 Apply/Cancel、语言切换保留草稿、主题图标刷新和草稿页面释放。
