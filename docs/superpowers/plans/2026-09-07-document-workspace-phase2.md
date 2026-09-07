# 文件工作区拆分实现计划（阶段二）

> **面向执行者：** 使用 executing-plans 与 test-driven-development 逐项实施。用户已批准保持布局不变的阶段二范围，并要求连续执行、分步验证和中文提交，不另设确认关卡。

**目标：** 将文件标签生命周期、当前文档路由和按标签顺序保存会话从 MainWindow 抽入 DocumentWorkspace。

**架构：** DocumentWorkspace 继承已有 TabbedCrawlerWidget，不增加容器层或更换标签外观。它借用宿主的 WindowSession、SignalMux 和 QuickFindMux，三者必须比工作区长寿；MainWindow 继续管理动作、标题、状态栏、打开入口、压缩包和退出策略。

**技术栈：** C++20、Qt 6 Widgets/QtTest、ZzPureTools、Windows MSVC Release。

## 设计取舍与约束

- 仅拆分源文件不能建立责任边界；整体重写标签控件又会扩大行为变化。本次选择在现有标签控件上建立文档级接口。
- `openDocument(path, viewContext)` 创建并选择文档；已打开文件的跨窗口激活仍由 MainWindow 协调。
- `closeDocument(index)` 停止加载、移除标签、注销会话并延迟释放页面，返回关闭路径供宿主更新最近文件。无效索引返回空字符串。
- `restoreDocuments()` 返回恢复的页面列表，供宿主保留原来的跟随文件策略；组件恢复活动标签。
- `saveDocuments(geometry)` 使用当前标签顺序和原 ViewContext 保存，不改变存储格式。
- `currentDocumentChanged(CrawlerWidget*)` 在两个 mux 更新后通知宿主；空工作区传 nullptr。过滤结果页切换通过 `refreshQuickFindSelector()` 重绑快速查找。
- 保留 `documentTabs` 对象名、拖动排序、标签菜单、最后标签关闭、多窗口、语言与主题行为；不触碰搜索算法、编码、数据层、第三方版本和其他分支。

## 任务 1：工作区组件与行为测试

**文件：** 新增 `src/ui/include/documentworkspace.h`、`src/ui/src/documentworkspace.cpp`、`tests/ui2/documentworkspacetest.cpp`；修改两个相应 CMakeLists.txt。

- [ ] 编写真实临时日志测试：打开两个文件，关闭非活动文件不切换活动文档，关闭最后文件后会话无残留且页面被释放，再次打开成功。组件尚不存在时编译应明确失败于缺失头文件。
- [ ] 实现上述接口及 currentChanged 路由；保留原先的页面创建和异步加载方式。
- [ ] 验证动作路由切换：通过真实 SignalMux 转发 followSet，切换标签后只改变当前页面；空工作区发送动作和快速查找不访问旧页面。
- [ ] 验证拖动排序后的保存顺序，并用相同窗口 ID 恢复会话。例：打开 A、B，移动 B 到首位，保存后 `SessionInfo::openFiles(id)` 顺序必须为 B、A。
- [ ] 构建新目标、运行 `ctest --test-dir out/ui-vs -C Release -R document_workspace --output-on-failure`，通过后中文提交。

## 任务 2：主窗口接入

**文件：** 修改 `src/ui/include/mainwindow.h`、`src/ui/src/mainwindow.cpp`，扩展 `tests/ui2/applicationtranslationtest.cpp`。

- [ ] 补充真实主窗口测试：关闭最后文件后的标题、菜单禁用及再次打开；跨窗口重复打开不新增文件页。
- [ ] 将 `mainTabWidget_` 换为 DocumentWorkspace，构造时传入 session 与 mux；将当前索引回调改为页面指针回调。
- [ ] loadFile 保留打开策略和旧视图上下文查询，仅将创建和标签选择委托 `openDocument`；closeTab 委托关闭并按发起者更新最近文件。
- [ ] reloadSession、writeSettings 和过滤页切换分别委托恢复、保存和快速查找路由；移除 MainWindow 对 mux 当前文档和标签增删的直接管理。
- [ ] 运行翻译、最后标签、运行时、多窗口/会话及生命周期测试，通过后中文提交。

## 任务 3：完整验收与学习文档

**文件：** 更新 `docs/UI_CODE_GUIDE.md`、本计划及总体路线的阶段状态。

- [ ] 全量 Release 构建和完整 CTest，生成隔离构建内的带依赖运行目录，不改用户数据。
- [ ] 检查生产调用者都走文档接口；检查 diff、析构顺序、会话和组件责任边界，处理审查发现。
- [ ] 更新学习文档的工作区入口、调用顺序和验收结果；记录未具备的实机验证，不宣称跨平台通过。
- [ ] 中文提交并保留当前开发分支，不合并、不推送。

## 执行记录

- 起点 `93e57023`，沿用 `.worktrees/zzpuretools-ui-refactor` 与 `codex/zzpuretools-ui-refactor`，工作区干净。定向基线 6/6 通过，原 master 的未提交文件保持不动。
