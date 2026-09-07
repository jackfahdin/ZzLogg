# 日志页面与搜索面板拆分计划（阶段三）

> **执行方式：** 依照 executing-plans 和 test-driven-development 连续实施。用户已经批准原布局不变的分阶段重构，并要求验证后分步中文提交，不重复等待确认。

**目标：** 将 CrawlerWidget 中的搜索控件构造和页面布局拆出，保留日志渲染、搜索状态机和数据接口。

**架构：** SearchPanel 是独立 QWidget，拥有输入、选项、搜索按钮、历史菜单和信息行；LogPage 是 QSplitter 布局基类，组合主视图、搜索面板和结果标签。CrawlerWidget 继承 LogPage，继续控制数据、搜索、结果映射、自动刷新和快捷键，通过面板信号接收用户意图。

**技术栈：** C++20、Qt 6 Widgets/QtTest、ZzPureTools，现有 Windows Release 工具链。

## 范围与取舍

- 仅挪动 setup 的源文件不能消除控件所有权混杂；一次替换为全新的框架搜索控件可能改变操作语义。采用已有 Qt 控件组合，继续由全局 ZzPureTools 样式和主题绘制。
- 搜索面板提供命名控件访问器，供现有业务的上下文恢复、快捷键及结果状态更新；不在 CrawlerWidget 保留另一套指针成员。
- 面板发出 searchRequested、stopRequested、patternEdited、patternSelected、各选项变化和历史菜单请求。过滤任务和持久化仍在 CrawlerWidget/原数据层。
- 面板翻译沿用 `CrawlerWidget` 翻译上下文；中英文切换不重建控件，不丢输入、选项与历史。图标仍由已有 IconLoader 提供。
- LogPage 只创建原来的垂直分割和底部结果区域，保持控件排列、边距、结果页对象名以及 splitter 尺寸存储语义。
- 不修改搜索算法、编码、配置字段、默认搜索后端、第三方版本或其他分支。阶段四设置与辅助窗口不纳入本次。

## 任务 1：独立搜索面板

**文件：** 新建 `src/ui/include/searchpanel.h`、`src/ui/src/searchpanel.cpp`、`tests/ui2/searchpaneltest.cpp`；修改对应 CMakeLists.txt。

- [ ] 先写面板测试：Enter 和按钮只发出一次搜索请求；清除只清输入；停止发出停止请求；选项点击发出对应信号；更新历史保留用户正在编辑的内容。缺少头文件时构建必须失败。
- [ ] 从旧 setup 迁入搜索行控件和默认值；上下文菜单由面板拥有并保持 Popup；提供控件访问器、`updateHistory(QStringList)`、`retranslateUi()`、`loadIcons(IconLoader&)`。
- [ ] 将控件事件转成面板意图信号，面板不启动数据搜索或写用户配置；语言变化只更新文案。
- [ ] 构建 `zzlogg_search_panel_test`，运行 CTest `search_panel`，验证通过后中文提交。

## 任务 2：日志页面组合与业务接入

**文件：** 新建 `src/ui/include/logpage.h`、`src/ui/src/logpage.cpp`；修改 `crawlerwidget.h/.cpp`、CMake 和现有 UI 测试的私有访问辅助函数。

- [ ] 新增真实主窗口搜索回归，要求页面包含独立 SearchPanel；接入前断言失败，避免只验证一份未使用的组件。
- [ ] `LogPage::compose(mainView, searchPanel, firstResults)` 保持垂直分割和原底部排列，结果标签由 `resultsTabs()` 暴露。CrawlerWidget 不再自行构造布局。
- [ ] 替换原搜索控件成员为一个 SearchPanel 指针，访问命名控件；接入搜索、停止、选项与菜单意图。保留原状态机处理顺序、结果页创建/关闭、快速查找及快捷键归属。
- [ ] 控件翻译、图标和历史更新委托面板；动态结果标题和搜索进度文案仍由业务层更新。
- [ ] 验证普通、正则、布尔、反选搜索，多结果保留/切换/关闭、取消和清除，以及持续追加刷新；运行语言、主题、会话与生命周期定向测试，通过后中文提交。

## 任务 3：完整验收与文档

**文件：** 更新学习指南、本计划与总体路线。

- [ ] 独立代码审查新组件所有权、翻译上下文、信号次数和页面接入，修复真实缺陷后复审。
- [ ] 全量 Release 构建和完整 CTest；通过现有 fixture 更新带依赖运行目录，不生成 ZIP，不接触用户设置。
- [ ] Windows 原生执行布局和搜索用例，检查深浅色、中英文截图，记录实际平台限制。
- [ ] 更新学习入口与执行结果，检查文档链接；中文提交，保留开发分支，不合并、不推送。

## 执行记录

- 基于 `5bfc5b4c`，现有隔离分支 `codex/zzpuretools-ui-refactor` 工作区干净，六项定向基线通过。继续沿用当前 worktree，原 master 未提交内容不动。
