# 浅色主题传播修复实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让 ZzLogg 从深色切换到浅色主题后，中央日志区及其辅助区域立即、完整地使用浅色调色板。

**架构：** 移除 Fluent 文档标签上的旧 QSS 传播边界，再让日志自绘缓存和搜索提示栏正确响应调色板变化。旧 UI 回退路径和语义高亮颜色保持不变。

**技术栈：** C++17、Qt 6 Widgets、Qt Test、ZzFluentStyle、CMake/CTest

---

## 文件结构

- 修改：`tests/ui2/runtimecontracttest.cpp`：覆盖真实窗口中深色到浅色的主题传播与渲染行为。
- 修改：`src/ui/src/tabbedcrawlerwidget.cpp`：Fluent 模式下取消旧标签栏 QSS。
- 修改：`src/ui/src/abstractlogview.cpp`：主题变化时刷新绘制缓存，并使用调色板绘制结构边栏。
- 修改：`src/ui/src/crawlerwidget.cpp`：调色板变化时刷新搜索提示栏默认色。

### 任务 1：建立主题传播回归测试

- [ ] **步骤 1：编写失败的真实窗口测试**

在 `RuntimeContractTest::decoratesRealWindowsAndRoutesSemanticState()` 中创建临时日志，调用 `MainWindow::loadFileNonInteractive()`，等待 `AbstractLogView` 可见；从深色切到浅色后断言：

```cpp
QCOMPARE( logView->viewport()->palette().color( QPalette::Base ), QColor( "#ffffff" ) );
QVERIFY( renderedPixel.lightness() > 200 );
QVERIFY( !documentTabs->testAttribute( Qt::WA_StyleSheet ) );
```

- [ ] **步骤 2：运行测试并确认旧代码失败**

运行：`cmake --build build/windows-msvc --target zzlogg_runtime_contract_test --config Release && ctest --test-dir build/windows-msvc -C Release -R zzlogg_runtime_contract --output-on-failure`

预期：FAIL，日志视口仍为深色或文档标签仍带 `WA_StyleSheet`。

- [ ] **步骤 3：提交回归测试**

```bash
git add tests/ui2/runtimecontracttest.cpp
git commit -m "test: cover light theme propagation"
```

### 任务 2：修复主题传播与日志自绘颜色

- [ ] **步骤 1：写最小实现**

在 `TabbedCrawlerWidget` 构造函数中，仅当 `zzlogg.fluentUi` 为假时应用原有 QSS。在 `AbstractLogView::changeEvent()` 中处理 `PaletteChange`、`ApplicationPaletteChange` 和 `StyleChange`，将 `textAreaCache_.invalid_` 置为 `true` 并刷新视口；边栏背景使用 `QPalette::AlternateBase`，普通项目符号和行号文字使用 `QPalette::Text`。

- [ ] **步骤 2：刷新搜索提示栏默认调色板**

让 `CrawlerWidget::changeEvent()` 在 `PaletteChange` 或 `ApplicationPaletteChange` 时同步更新 `searchInfoLineDefaultPalette_`，但只在 `StyleChange` 时重载图标。

- [ ] **步骤 3：运行目标测试并确认通过**

运行：`cmake --build build/windows-msvc --target zzlogg_runtime_contract_test --config Release && ctest --test-dir build/windows-msvc -C Release -R zzlogg_runtime_contract --output-on-failure`

预期：PASS。

- [ ] **步骤 4：提交实现**

```bash
git add src/ui/src/tabbedcrawlerwidget.cpp src/ui/src/abstractlogview.cpp src/ui/src/crawlerwidget.cpp
git commit -m "fix: propagate runtime theme palettes"
```

### 任务 3：全量验证与主线集成

- [ ] **步骤 1：构建全部目标并运行全套测试**

运行：`cmake --build build/windows-msvc --config Release && ctest --test-dir build/windows-msvc -C Release --output-on-failure`

预期：构建成功，全部测试通过。

- [ ] **步骤 2：检查分支和工作树**

运行：`git status --short && git log --oneline --decorate -5`

预期：仅有本次已提交变更，历史中没有 merge commit。

- [ ] **步骤 3：快进主线并推送**

在主工作树执行：

```bash
git merge --ff-only codex/light-theme-propagation
git push origin master
```

预期：`master` 快进到修复提交，远端推送成功。
