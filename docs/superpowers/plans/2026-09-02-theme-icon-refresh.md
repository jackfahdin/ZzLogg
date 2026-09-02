# 主题图标刷新修复实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让所有由 `IconLoader` 生成并长期缓存的图标在浅色、深色主题切换后立即采用正确的前景色。

**架构：** 保留现有 PNG 资源和 `IconLoader` 明暗判断，在持有缓存的窗口与文档控件边界响应三种主题视觉事件并重新加载图标。用真实运行时窗口验证图标可见像素的亮度变化。

**技术栈：** C++17、Qt 6 Widgets、Qt Test、ZzFluentStyle、CMake/CTest

---

## 文件结构

- 修改：`tests/ui2/runtimecontracttest.cpp`：覆盖深色、浅色主题之间工具栏图标的真实像素变化。
- 修改：`src/ui/src/mainwindow.cpp`：调色板变化时刷新工具栏动作和收藏菜单图标。
- 修改：`src/ui/src/crawlerwidget.cpp`：调色板变化时同步刷新搜索区图标。
- 修改：`src/ui/src/tabbedcrawlerwidget.cpp`：调色板变化时同步刷新标签状态图标。

### 任务 1：建立工具栏图标回归测试

- [x] **步骤 1：编写失败测试**

在真实 `MainWindow` 中找到主工具栏的打开动作，渲染其 `QIcon` 并统计非透明像素的平均亮度。深色主题下断言亮度大于 200，切到浅色后等待亮度小于 80，再切回深色并等待亮度大于 200。

- [x] **步骤 2：验证红灯**

运行：`cmake --build out/ui-vs --target zzlogg_runtime_contract_test --config RelWithDebInfo`，然后运行 `ctest --test-dir out/ui-vs -C RelWithDebInfo -R zzlogg_ui2.runtime_contract --output-on-failure`。

预期：浅色阶段失败，动作仍持有深色主题生成的亮色图标。

### 任务 2：实现调色板事件刷新

- [x] **步骤 1：编写最小实现**

让 `MainWindow`、`CrawlerWidget`、`TabbedCrawlerWidget` 在 `StyleChange`、`PaletteChange`、`ApplicationPaletteChange` 任一事件到达时调用现有 `loadIcons()`；保留各控件已有的调色板缓存刷新逻辑。

- [x] **步骤 2：验证绿灯**

重新构建并运行 `zzlogg_ui2.runtime_contract`，预期通过。

- [x] **步骤 3：运行全量验证**

运行 `cmake --build out/ui-vs --config RelWithDebInfo` 和 `ctest --test-dir out/ui-vs -C RelWithDebInfo --output-on-failure`，预期全部构建和测试通过。
