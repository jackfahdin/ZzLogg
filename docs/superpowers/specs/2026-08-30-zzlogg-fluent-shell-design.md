# ZzLogg Fluent 窗口外壳设计

> 状态：已确认
>
> 确认日期：2026-08-30
>
> 第一阶段目标平台：Windows + MSVC 完整验收；Linux 与 macOS 保证编译

## 1. 目标

第一阶段为 ZzLogg 增加一个独立的 Qt 6 UI 实验目标 `zzlogg_ui2`。该目标继续使用现有 Klogg 工作区，仅将系统标题栏和独立菜单栏替换为 ZzPureTools 提供的 Fluent 标题栏，并使用 ZzPureTools 的窗口框架与主题系统统一现有 Qt Widgets 的外观。

旧目标 `klogg` 和 `klogg_portable` 保持原有窗口路径，作为稳定版本和行为对照。新目标验证成熟后，是否替换正式入口由后续设计决定。

## 2. 明确范围

### 2.1 第一阶段包含

- 新建独立可执行目标 `zzlogg_ui2`。
- 以 Git submodule 方式接入 ZzPureTools。
- 使用 `ZzFluentTitleBar` 合并应用图标、现有菜单、居中标题、主题与置顶命令和窗口控制按钮。
- 使用 `ZzWindowAgent` 管理无边框窗口、拖动命中和系统按钮。
- 使用 `ZzThemeController` 与 Fluent Style 为现有标准 Qt Widgets 提供统一主题。
- 默认跟随系统主题，并允许用户选择跟随系统、浅色或深色。
- 在现有设置窗口中增加主题选项。
- 保持多窗口、会话恢复和现有 QAction 行为。

### 2.2 第一阶段不包含

- 不重写或重新布局文件信息工具栏。
- 不重写文件标签页、原始日志视图、过滤搜索条、过滤结果视图和分割器。
- 不引入导航栏、命令栏、侧边抽屉或新的工作区模式。
- 不修改日志读取、过滤、正则、文件监视和会话业务规则。
- 不引入 ZzPureTools 的导航、页面路由或完整应用装配层。
- 不增加强调色、背景材质或自定义主题编辑器。
- 不生成 `zzlogg_ui2_portable`。
- 不在第一阶段替换正式 `klogg` 入口。

## 3. 已确认的视觉结构

窗口从上到下保持以下结构：

```text
ZzFluentTitleBar
├── 应用图标
├── 文件 / 编辑 / 查看 / 工具 / 编码 / 收藏夹 / 帮助
├── 当前文件名 — ZzLogg（相对整个窗口绝对居中）
├── 主题 / 窗口置顶
└── 最小化 / 最大化或还原 / 关闭

现有文件信息工具栏
现有文件标签页
现有原始日志视图
现有过滤搜索条
现有过滤结果视图
```

普通宽度下菜单横向展开。窗口宽度不足时使用 `ZzTitleBarMenuDisplayMode::Adaptive` 自动折叠菜单。菜单、图标和系统按钮是非拖动交互区域；标题与其余空白区域是窗口拖动区域。

标题规则：

- 未打开文件时显示 `ZzLogg`。
- 打开文件时显示 `当前文件名 — ZzLogg`。
- 标题随活动标签切换即时更新。
- 超长文件名按 `ZzFluentTitleBar` 的既有行为从末尾省略。
- 标题不能覆盖左侧菜单或右侧窗口按钮。
- 标题栏保留 `ZzFluentTitleBar` 标准的主题按钮和置顶按钮。主题按钮提供跟随系统、浅色和深色；置顶按钮控制当前窗口置顶状态。

## 4. 总体架构

```text
zzlogg_ui2
├── 现有 KloggApp
├── ZzLoggUiRuntime（应用级）
│   ├── ZzWindowKitBootstrap
│   ├── ZzThemeController
│   ├── ZzFluentStyle
│   └── 窗口装饰入口
├── 现有 MainWindow
│   ├── 工具栏
│   ├── 文件标签页
│   ├── 原始日志视图
│   ├── 搜索与过滤栏
│   └── 过滤结果视图
└── ZzLoggFluentShell（每个窗口一个）
    ├── ZzFluentTitleBar
    └── ZzWindowAgent
```

`ZzLoggFluentShell` 是附着到现有 `MainWindow` 的 QObject 适配器。它不继承 `MainWindow`，不复制现有窗口，也不访问日志业务对象。它只负责标题栏、菜单、窗口 Chrome、标题同步和主题同步。

## 5. ZzPureTools 接入

ZzPureTools 作为 Git submodule 放置在：

```text
3rdparty/vendor/ZzPureTools
```

第一阶段固定到本设计评估过的提交：

```text
f8f60baeec8eed7daae93ff6008a0b93879cfe33
```

`.gitmodules` 必须包含完整 URL 与路径。CMake 不通过 `FetchContent` 或其他方式自动下载缺失依赖；submodule 缺失时配置阶段直接报告初始化命令。

ZzLogg 只链接以下目标：

- `Zz::WindowKit`
- `Zz::FluentFoundation`
- `Zz::FluentUI`

不链接 `Zz::AppCore` 和 `Zz::PureTools`。`zzlogg_ui2` 使用 C++20；现有日志内核目标保持当前 C++17 设置，除非某个具体目标因链接接口明确要求升级。

配置 submodule 前显式关闭 ZzPureTools 的测试、示例、基准测试、静态分析和发布打包选项，并用 `EXCLUDE_FROM_ALL` 加入构建。ZzPureTools 第一方库保持其默认共享库形式；这些选项的作用域不得改变 ZzLogg 现有第三方目标的构建类型、告警策略或 C++ 标准。

## 6. 应用与窗口所有权

`ZzLoggUiRuntime` 由 `zzlogg_ui2` 应用入口创建并在所有窗口之前完成初始化。它拥有应用级主题控制器和 Fluent Style，确保多个窗口共享同一主题状态。

`KloggApp` 增加一个可选窗口装饰回调：

```text
MainWindow 构造并完成原 UI 初始化
    -> KloggApp 调用可选装饰回调
    -> ZzLoggUiRuntime 为窗口安装 ZzLoggFluentShell
    -> 窗口 show()
```

旧版入口不设置回调，因此不创建或链接 Fluent Shell。恢复会话和新建窗口都经过同一回调，不能出现只有第一个窗口应用新外壳的情况。

每个 Shell 的生命周期绑定到对应窗口：

- 标题栏由 `MainWindow` 的 QWidget 对象树拥有。
- `ZzWindowAgent` 由 Shell 独占。
- Shell 只观察应用级 `ZzThemeController`。
- 窗口销毁时，代理、信号连接和 Shell 按安全顺序释放。

## 7. 菜单迁移

菜单迁移复用现有 QAction 和 QMenu 实例，不重新创建动作。这样可保留快捷键、启用状态、勾选状态、翻译和既有信号连接。

迁移采用事务式顺序：

1. 创建但不安装 `ZzFluentTitleBar`。
2. 创建并附着 `ZzWindowAgent`。
3. 完成窗口 Chrome 配置。
4. 将现有顶层菜单按原顺序迁移到标题栏的 `QMenuBar`。
5. 将 QMenu 所有权安全转移到新标题栏对象树。
6. 用 Fluent 标题栏替换原菜单栏占位。
7. 更新标题与最大化状态。

任何步骤失败时，已完成的临时操作必须回滚，原菜单栏继续可用，不能留下空菜单或重复菜单。

## 8. 标题数据流

`MainWindow` 提供与翻译无关的语义信号：

```cpp
void activeDocumentNameChanged(const QString &fileName);
```

打开、关闭、重命名或切换活动文件时发出。Shell 根据文件名构造展示标题，同时更新 `ZzFluentTitleBar` 与原生 `windowTitle`，保证标题栏、任务栏、Alt+Tab 和辅助功能读取一致。

旧版 `klogg` 保留现有标题生成方式；新增信号不改变旧版窗口标题。

## 9. 主题数据流

现有 Configuration 增加应用级主题模式：

```text
System
Light
Dark
```

```text
Configuration
    -> ZzLoggUiRuntime
    -> ZzThemeController::setMode()
    -> snapshotChanged
    -> ZzFluentStyle 与所有 ZzFluentTitleBar
```

- 默认值为 `System`。
- `System` 模式监听操作系统主题变化并即时刷新。
- `Light` 与 `Dark` 固定普通主题模式。
- 系统高对比度模式始终优先于普通主题选择。
- 无效配置值恢复为 `System` 并写回规范值。
- 主题设置不随文件、标签或窗口会话分别保存。
- 主题切换不要求重启应用。

标题栏主题按钮与设置窗口写入同一个应用级配置。`themeModeRequested` 先交给 `ZzLoggUiRuntime` 应用并写入 Configuration，再由主题快照同步所有窗口；标题栏不直接修改全局主题。

置顶按钮只控制当前窗口。Shell 接收 `alwaysOnTopRequested`，安全更新窗口标志并将实际结果通过 `setAlwaysOnTop()` 回写标题栏。第一阶段不跨应用重启保存置顶状态。

## 10. 错误处理与降级

新 UI 外壳失败不能阻止日志工具启动：

- Bootstrap 初始化失败时记录技术错误、显示一次错误对话框，并使用现有系统标题栏和菜单栏。
- `attach()` 或 `configureChrome()` 失败时放弃临时 Fluent 标题栏，保留原菜单栏。
- 单个窗口安装失败只影响该窗口。
- 主题效果不支持时使用普通 Qt Palette。
- Debug 构建记录完整结果错误与 WindowKit 能力信息。

降级不能掩盖验收失败。Windows 自动测试和人工验收必须确认 Shell 成功安装；处于降级状态视为第一阶段不通过。

## 11. 测试设计

### 11.1 单元测试

- 无文件、普通文件名、Unicode 和超长文件名的标题格式。
- `System / Light / Dark` 配置序列化。
- 无效主题值恢复为 `System`。
- 菜单顺序、分隔符和 QAction 实例身份保持不变。

### 11.2 Shell 集成测试

使用最小 `QMainWindow` 测试宿主验证：

- Fluent 标题栏安装成功，原菜单栏不再占位。
- QAction 快捷键、checked 与 enabled 状态保持同步。
- 窗口按钮发送正确意图。
- 菜单、图标与系统按钮属于非拖动区域。
- 标题和空白区域可拖动。
- 主题变化传播到标题栏和标准 Widgets。
- 标题栏主题命令修改应用级设置并同步全部窗口。
- 置顶命令只修改当前窗口并回写实际状态。
- Shell 销毁后没有悬空连接或重复释放。
- 注入失败时原菜单栏仍可使用。

### 11.3 应用冒烟测试

启动 `zzlogg_ui2` 并通过测试开关自动退出，验证：

- Shell 实际安装成功而非降级。
- 可以打开测试日志。
- 可以切换文件标签、搜索和过滤。
- 新建第二个窗口后标题和主题同步。
- 恢复窗口会话后每个窗口都安装 Shell。

## 12. Windows 人工验收

- 图标、菜单、标题和系统按钮顺序正确。
- 标题相对整个窗口真正居中。
- 主题按钮和置顶按钮状态、工具提示与命中区域正确。
- 菜单弹出位置、Alt 快捷键和键盘导航正确。
- 标题栏拖动、双击最大化、系统菜单、最小化、还原和关闭正确。
- 浅色、深色、跟随系统和高对比度行为正确。
- 100%、125%、150% 和 200% 缩放下无重叠或错误命中区域。
- 多显示器不同 DPI 间移动窗口时正确刷新。
- 原始日志与过滤结果的滚动、选择、搜索和跟随文件行为与旧版一致。
- 新旧版本打开同一测试文件时搜索和过滤结果一致。

## 13. 构建验收

- `klogg` 构建并保持旧窗口路径。
- `klogg_portable` 构建并保持现有便携行为。
- `zzlogg_ui2` 构建并成功安装 Fluent Shell。
- `klogg_grep` 仍不进入默认构建。
- 默认构建不生成 `zzlogg_ui2_portable`。
- Windows + MSVC 完成构建、自动测试和人工交互验收。
- Linux 与 macOS 至少成功配置并编译 `zzlogg_ui2`。

## 14. 完成条件

第一阶段只有在以下条件同时满足时完成：

1. 新旧可执行目标可以并存。
2. 新目标只改变标题栏、菜单承载方式与主题，不改变工作区结构。
3. Windows 下 Fluent Shell 未降级且窗口系统行为正常。
4. 三种主题选择和系统高对比度行为符合设计。
5. 多窗口、会话恢复、菜单、快捷键和日志核心工作流通过验收。
6. 旧版 `klogg` 行为未发生回归。
