# 设置与辅助窗口实施计划（阶段四）

> 按已批准路线连续执行，使用 executing-plans、test-driven-development 和独立代码审查；用户已要求无需逐步确认，验证后使用中文标题及详细正文提交。

**目标：** 分清设置展示与持久化职责，补齐辅助窗口动态语言、主题与对象生命周期。

**架构：** 保留 Designer 标签布局及全部配置键。OptionsDialog 保留保存、迁移与按钮事务；optionsdialogpages.cpp 集中页面初始化、配置回填与动态文案。辅助窗口使用现有 QApplication 的 ZzFluentStyle，不新增主题控制器，不改变高亮自定义颜色。

**技术栈：** Qt 6 Widgets、ZzPureTools、现有 Windows Release 与 QtTest。

## 方案与边界

- 采用职责拆分和现有控件事件适配；不重建全部设置页类（会增加迁移风险），也不只更换文件名。
- 保留设置标签顺序、搜索/文件监控/字体/主题/存储分组、Apply/OK/Cancel 语义；不改变存储服务和迁移实现。
- 高亮、预定义过滤在 LanguageChange 下只更新静态标签，保留正在编辑的内容、选择和配色。PaletteChange/StyleChange 下重新加载工具图标，延迟回调绑定窗口对象。
- 草稿文字转换功能不变；新增可重译的动作/说明和稳定页编号，关闭页面释放对象，保留加号说明页。
- 第三阶段已知末行计数缺陷不在此次范围；不修改搜索内核或第三方依赖。

## 任务 1：设置展示边界

文件：optionsdialog.h/.cpp、新建 src/ui/src/optionsdialogpages.cpp、src/ui/CMakeLists.txt、tests/ui2/applicationtranslationtest.cpp。

- [ ] 在真实 OptionsDialog 测试中编辑多个页，Cancel 后重开值不变，Apply 后重开值一致；语言变化不丢草稿。
- [ ] 将 setupTabs/setupFontList/setupRegexp/setupStyles/setupEncodings/setupLanguageList、页面依赖启用、配置回填、字体/颜色展示、动态翻译迁入 optionsdialogpages.cpp。保留成员函数接口和翻译上下文；保存与迁移留在原文件。
- [ ] 给轮询 validator 指定 pollIntervalLineEdit 父对象；用 QPointer 在窗口销毁后验证释放。
- [ ] 构建并跑 application_translation、options_theme、storage_settings；通过后提交。

测试要点：

```cpp
const bool before = Configuration::get().useTextWrap();
OptionsDialog dialog;
dialog.wrapTextCheckBox->setChecked(!before);
dialog.reject();
QCOMPARE(Configuration::get().useTextWrap(), before);
```

## 任务 2：高亮与预定义过滤

文件：highlightersdialog、highlightersetedit、highlighteredit、predefinedfiltersdialog 的 .h/.cpp；应用集成测试。

- [ ] 先在已创建窗口切换中英文并比较标题/表头/选项，断言草稿不变；当前无 LanguageChange 处理时失败。
- [ ] 添加 changeEvent 和 loadIcons。调用各自 Designer retranslateUi；HighlighterEdit 原地更新正则选项；预定义过滤表头用既有 tr 上下文，不重建表格。
- [ ] 将所触及窗口中所有捕获 this 的 dispatchToMainThread 改为 dispatchToObject(callback, this)。移除居中复选框一次性固定 palette，继承统一主题。
- [ ] 验证窗口销毁后排队回调不再运行、暗浅主题图标像素发生变化、Cancel 不保存且 Apply 保存草稿；通过后提交。

```cpp
auto before = dialog.windowTitle();
MainWindow::installLanguage("zh_CN");
QCoreApplication::processEvents();
QVERIFY(dialog.windowTitle() != before);
QCOMPARE(dialog.filtersTableWidget->item(0, 1)->text(), QString("draft"));
```

## 任务 3：草稿区域及验收

文件：scratchpad.h/.cpp、tabbedscratchpad.h/.cpp、translations/klogg_zh_CN.ts 与 zh_TW.ts（以实际目录为准）、应用集成测试、学习指南及路线。

- [ ] 先测试切换语言后动作与标签变化、正文和撤销栈保留；关闭草稿页后 QPointer 为空，加号说明页不被快捷键关闭。
- [ ] 动作与转换标签使用 tr 和原地 retranslateUi；动作由工具栏拥有。页标题由稳定序号重译；按钮/快捷键统一 closeTab，removeTab 后 deleteLater。
- [ ] 为新增中文文案补简体和繁体翻译，不调用全库 lupdate 造成无关重排。
- [ ] 独立审查任务结果，完整 Release 构建、CTest、Windows 原生语言/主题截图和运行目录验证；修复本阶段回归。
- [ ] 更新文档并提交。保留分支，不合并推送；Linux/macOS 与人工多屏操作不宣称通过。

## 基线

7c57fec2；现有隔离 worktree 干净。application_translation、options_theme、storage_settings 三项定向基线通过。master 原未提交内容保持不动。
