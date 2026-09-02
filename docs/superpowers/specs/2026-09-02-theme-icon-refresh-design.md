# 主题图标刷新修复设计

## 目标

修复 ZzLogg 在运行时由深色切换到浅色主题后，工具栏及其他旧版 PNG 图标仍保留深色主题反色结果的问题。图标外观、工具栏布局和动作行为保持不变。

## 根因

`IconLoader` 根据所属控件当前的 `QPalette::Window` 判断背景明暗：浅色主题加载黑色原图，深色主题将灰度图标反色为白色。主题模式变化在 ZzPureTools 中属于纯颜色变化，只通过应用调色板传播 `PaletteChange`/`ApplicationPaletteChange`；`MainWindow`、`CrawlerWidget` 和 `TabbedCrawlerWidget` 却只在 `StyleChange` 时重新加载图标。因此动作持有的 `QIcon` 会保留切换前生成的像素。

## 方案选择

采用控件侧的最小兼容修复：所有持有长期图标缓存的控件在 `StyleChange`、`PaletteChange` 或 `ApplicationPaletteChange` 时统一重载图标。主窗口同时刷新依赖图标的收藏菜单；短生命周期对话框在创建时使用当前调色板，不增加无关重构。

不修改 ZzPureTools 的主题变化分类，因为纯颜色变化不应伪装成几何样式变化；也不引入新的 `QIconEngine` 或替换现有图标资产，因为这超出本次缺陷范围。

## 验证

扩展真实 UI 运行时契约测试：从深色主题创建窗口并记录工具栏动作图标的可见像素亮度，切换到浅色主题后等待图标重新生成，断言同一动作的图标由亮色变为深色；再切回深色并断言恢复。测试直接检查真实 `QAction`/`QToolButton` 图标，不使用 mock。
