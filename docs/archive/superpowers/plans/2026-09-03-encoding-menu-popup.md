# 编码菜单弹出修复实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 恢复主窗口“编码”菜单的正常弹出行为，并让“Auto/自动”选中项只出现在菜单内部。

**架构：** 编码菜单继续由 `EncodingMenu` 统一生成，但创建时直接接收菜单栏父对象，避免二次 `setParent()` 破坏 `QMenu` 的 `Qt::Popup` 属性。使用真实 `MainWindow` 和真实鼠标点击覆盖用户可见行为。

**技术栈：** C++17、Qt 6 Widgets、Qt Test、CMake/CTest

---

### 任务 1：恢复编码菜单弹出语义

**文件：**
- 修改：`tests/ui2/applicationtranslationtest.cpp`
- 修改：`src/ui/include/encodings.h`
- 修改：`src/ui/src/mainwindow.cpp`

- [x] **步骤 1：编写失败的测试**

在 `ApplicationTranslationTest` 中增加 `opensEncodingMenuFromMenuBar()`。构造并显示真实 `MainWindow`，取得 `encodingMenu` 及其顶层 action，通过 `QTest::mouseClick()` 点击菜单栏中的“Encoding”，然后断言 `encodingMenu` 可见、为弹出窗口，并且 `encodingAutoAction` 仍属于该菜单。

- [x] **步骤 2：运行测试验证失败**

运行：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_application_translation_test --parallel 1
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.application_translation$' --output-on-failure
```

预期：测试在编码菜单可见性或 `Qt::Popup` 契约处失败，证明现有普通 `setParent()` 会复现用户报告的问题。

- [x] **步骤 3：编写最少实现代码**

将生成函数改为接收父控件：

```cpp
static QMenu* generate( QActionGroup* actionGroup, QWidget* parent )
{
    QMenu* encodingsMenu = new QMenu( translatedTitle, parent );
    // 保留现有 action 构造逻辑
}
```

在 `MainWindow::createMenus()` 中调用 `EncodingMenu::generate( encodingGroup, menuBar() )`，删除随后普通 `setParent()` 的代码。

- [x] **步骤 4：运行测试验证通过**

重新构建并运行 `zzlogg_ui2.application_translation`，预期通过；再运行 `zzlogg_ui2.fluent_shell`，确认自定义标题栏迁移后菜单仍可弹出。

- [x] **步骤 5：运行完整验证**

运行 RelWithDebInfo 完整构建和 CTest，预期所有测试通过；运行 `git diff --check`，预期无格式错误。

- [x] **步骤 6：Commit**

```powershell
git add docs/superpowers/specs/2026-09-03-encoding-menu-popup-design.md docs/superpowers/plans/2026-09-03-encoding-menu-popup.md tests/ui2/applicationtranslationtest.cpp src/ui/include/encodings.h src/ui/src/mainwindow.cpp
git commit -m "fix: restore encoding menu popup"
```
