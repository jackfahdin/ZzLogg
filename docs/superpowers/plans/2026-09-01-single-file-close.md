# 单文件关闭实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 单文件状态继续显示文档标签和关闭按钮，并允许窗口保持打开的情况下关闭到零文档。

**架构：** 将 `TabbedCrawlerWidget` 的标签栏显隐规则集中到一个私有函数：零标签隐藏，至少一个标签显示。关闭模型、Session 和 UI2 Fluent shell 不改变；新增一个直接覆盖标签可见性和关闭信号的 Qt Test。

**技术栈：** C++17、Qt 6 Widgets/Test、CMake/CTest、MSVC 2026。

---

## 文件结构

- 创建 `tests/ui2/documenttabclosetest.cpp`：以轻量测试文档覆盖 0→1、1→0、2→1 三种状态和 close button 信号。
- 修改 `tests/ui2/CMakeLists.txt`：注册 `zzlogg_document_tab_close_test` 及 Windows Qt 运行环境。
- 修改 `src/ui/include/tabbedcrawlerwidget.h`：声明唯一的标签栏显隐更新函数。
- 修改 `src/ui/src/tabbedcrawlerwidget.cpp`：添加、删除标签后统一应用 `count() > 0` 规则。

### 任务 1：用测试锁定单文件标签行为并修复

**文件：**
- 创建：`tests/ui2/documenttabclosetest.cpp`
- 修改：`tests/ui2/CMakeLists.txt`
- 修改：`src/ui/include/tabbedcrawlerwidget.h`
- 修改：`src/ui/src/tabbedcrawlerwidget.cpp:102-156`

- [ ] **步骤 1：编写失败的单文件标签测试**

在 `tests/ui2/documenttabclosetest.cpp` 中创建可被 `addCrawler<T>()` 接受的轻量 QWidget，并隔离持久设置：

```cpp
#include <QtTest>

#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>

#include "persistentinfo.h"
#include "tabbedcrawlerwidget.h"

const bool PersistentInfo::ForcePortable = false;

class TestCrawler final : public QWidget {
    Q_OBJECT
  Q_SIGNALS:
    void dataStatusChanged( DataStatus status );
};

class DocumentTabCloseTest final : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void initTestCase();
    void oneDocumentShowsCloseableTab();
    void closingOneOfTwoKeepsRemainingTabVisible();

  private:
    QTemporaryDir settingsRoot_;
};

void DocumentTabCloseTest::initTestCase()
{
    QVERIFY( settingsRoot_.isValid() );
    QVERIFY( setPersistentSettingsOverrideForProcess( QSettings::IniFormat,
                                                       settingsRoot_.path() ) );
}

void DocumentTabCloseTest::oneDocumentShowsCloseableTab()
{
    TabbedCrawlerWidget tabs;
    tabs.setTabsClosable( true );
    tabs.show();

    auto* crawler = new TestCrawler;
    tabs.addCrawler( crawler, QStringLiteral( "single.log" ) );
    auto* tabBar = tabs.findChild<CrawlerTabBar*>();
    QVERIFY( tabBar != nullptr );
    QTRY_VERIFY( tabBar->isVisible() );

    QWidget* closeButton = tabBar->tabButton( 0, QTabBar::RightSide );
    if ( closeButton == nullptr ) {
        closeButton = tabBar->tabButton( 0, QTabBar::LeftSide );
    }
    QVERIFY( closeButton != nullptr );

    connect( &tabs, &QTabWidget::tabCloseRequested, &tabs,
             [ &tabs ]( int index ) { tabs.removeCrawler( index ); } );
    QTest::mouseClick( closeButton, Qt::LeftButton );

    QTRY_COMPARE( tabs.count(), 0 );
    QTRY_VERIFY( !tabBar->isVisible() );
}

void DocumentTabCloseTest::closingOneOfTwoKeepsRemainingTabVisible()
{
    TabbedCrawlerWidget tabs;
    tabs.setTabsClosable( true );
    tabs.show();
    tabs.addCrawler( new TestCrawler, QStringLiteral( "first.log" ) );
    tabs.addCrawler( new TestCrawler, QStringLiteral( "second.log" ) );

    auto* tabBar = tabs.findChild<CrawlerTabBar*>();
    QVERIFY( tabBar != nullptr );
    tabs.removeCrawler( 0 );

    QCOMPARE( tabs.count(), 1 );
    QTRY_VERIFY( tabBar->isVisible() );
}

QTEST_MAIN( DocumentTabCloseTest )
#include "documenttabclosetest.moc"
```

将测试注册到 `tests/ui2/CMakeLists.txt`：

```cmake
add_executable(zzlogg_document_tab_close_test documenttabclosetest.cpp)
target_link_libraries(zzlogg_document_tab_close_test PRIVATE
  klogg_ui Qt6::Test Qt6::Widgets)
set_target_properties(zzlogg_document_tab_close_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.document_tab_close
         COMMAND zzlogg_document_tab_close_test -platform offscreen)
if(WIN32)
  set_property(TEST zzlogg_ui2.document_tab_close PROPERTY ENVIRONMENT
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
  set_property(TEST zzlogg_ui2.document_tab_close APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()
```

- [ ] **步骤 2：构建并运行测试，确认旧行为失败**

运行：

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_document_tab_close_test --parallel 8
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.document_tab_close$"
```

预期：`oneDocumentShowsCloseableTab` 在 `tabBar->isVisible()` 断言处失败；这证明测试捕获的是单标签隐藏问题。

- [ ] **步骤 3：集中标签栏显隐规则**

在 `TabbedCrawlerWidget` 私有区声明：

```cpp
void updateTabBarVisibility();
```

在 `tabbedcrawlerwidget.cpp` 中实现并替换分散条件：

```cpp
void TabbedCrawlerWidget::updateTabBarVisibility()
{
    myTabBar_.setVisible( count() > 0 );
}
```

构造函数在 `setTabBar()` 后调用一次；`addTabBarItem()` 在 `setCurrentIndex()` 后调用；`removeCrawler()` 在 `QTabWidget::removeTab()` 后调用。删除原来的 `count() > 1` 和 `count() <= 1` 分支。

- [ ] **步骤 4：运行聚焦测试和现有窗口关闭测试**

运行：

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_document_tab_close_test klogg_itests --parallel 8
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.document_tab_close$"
out/ui2-vs/output/RelWithDebInfo/klogg_itests.exe "Scenario: Main window tests" -platform offscreen
```

预期：新测试通过；现有 MainWindow 场景保持 23 个断言全部通过，证明 `Ctrl+W` 和零文档状态没有退化。

- [ ] **步骤 5：运行全部 UI2 回归测试**

运行：

```powershell
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\."
```

预期：全部 UI2 测试通过。

- [ ] **步骤 6：提交单文件关闭修复**

```powershell
git add src/ui/include/tabbedcrawlerwidget.h src/ui/src/tabbedcrawlerwidget.cpp tests/ui2/documenttabclosetest.cpp tests/ui2/CMakeLists.txt
git commit -m "fix: keep the last document tab closeable"
```
