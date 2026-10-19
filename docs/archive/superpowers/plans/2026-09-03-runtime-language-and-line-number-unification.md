# 运行时语言刷新与行号选项合并实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让已打开的设置窗口、主窗口和日志搜索控件在切换英文、简体中文、繁体中文后立即完整重译，并将两套行号设置收敛为一个可迁移、可持久化、同时控制主视图与过滤视图的选项。

**架构：** 各个长期存活的 Qt Widget 负责自身构造期和动态文本，通过集中 `retranslateUi()` 与 `QEvent::LanguageChange` 刷新，顶层 `MainWindow` 只协调其拥有的菜单、动作和状态。配置层把旧的两个行号键迁移到单一新键，界面层只暴露一个动作，`CrawlerWidget` 把同一个值应用到两种日志视图。所有行为先由 Qt Test 写出失败用例，再用最小实现使测试通过。

**技术栈：** C++20、Qt 6 Widgets/LinguistTools/Test、CMake/CTest、QSettings

---

## 文件结构与职责

- `src/settings/include/configuration.h`：提供单一 `lineNumbersVisible()` / `setLineNumbersVisible(bool)` 和单一字段；Task 1 暂留映射到该字段的兼容 wrapper，Task 2 更新全部调用点后删除旧 API。
- `src/settings/src/configuration.cpp`：读取新键、执行旧键迁移、保存新键并清理旧键。
- `src/settings/include/shortcuts.h`、`src/settings/src/shortcuts.cpp`：把快捷键稳定元数据与当前语言的显示名称分离，消除静态容器冻结翻译的问题。
- `src/ui/include/mainwindowtext.h`、`src/ui/src/mainwindowtext.cpp`：把两个行号动作源文本收敛为一个 `Line &numbers`。
- `src/ui/include/mainwindow.h`、`src/ui/src/mainwindow.cpp`：单一行号动作、遗漏菜单/托盘/标题/状态重译，以及编码菜单引用的生命周期管理。
- `src/ui/include/abstractlogview.h`：提供只读行号可见性查询，支持真实 UI 状态断言。
- `src/ui/include/crawlerwidget.h`、`src/ui/src/crawlerwidget.cpp`：将统一行号设置应用到两种视图，并刷新搜索区动态文本。
- `src/ui/include/optionsdialog.h`、`src/ui/src/optionsdialog.cpp`：设置页动态条目原位重译，语言切换不再触发重启提示。
- `src/ui/include/storagelocationpage.h`、`src/ui/src/storagelocationpage.cpp`：保存动态标签指针并在语言变化时刷新显示与验证文本。
- `src/ui/include/predefinedfilterscombobox.h`、`src/ui/src/predefinedfilterscombobox.cpp`：刷新预定义过滤器标题。
- `src/ui/include/quickfindwidget.h`、`src/ui/src/quickfindwidget.cpp`、`src/ui/include/qfnotifications.h`：Quick Find 控件和通知文本支持当前语言。
- `src/ui/include/encodings.h`：为生成后的编码菜单提供可重复调用的重译入口，不重建 action group。
- `src/app/i18n/en.ts`、`src/app/i18n/zh_CN.ts`、`src/app/i18n/zh_TW.ts`：更新受影响源文本并补齐简体、繁体翻译。
- `tests/ui2/linenumberconfigurationtest.cpp`：覆盖新键读写、默认值和所有旧键迁移分支。
- `tests/ui2/applicationtranslationtest.cpp`：在控件已经存活的条件下覆盖设置页、主窗口、文档搜索区和 Quick Find 的即时重译。
- `tests/ui2/linenumberuitest.cpp`：覆盖单一 View 动作以及主视图、过滤视图同步切换。
- `tests/ui2/CMakeLists.txt`：注册新增测试并复用翻译资源、offscreen 平台及 Windows 运行库环境。

## Task 1：统一配置模型并迁移旧行号键

**文件：**

- Create: `tests/ui2/linenumberconfigurationtest.cpp`
- Modify: `tests/ui2/CMakeLists.txt`
- Modify: `src/settings/include/configuration.h`
- Modify: `src/settings/src/configuration.cpp`

- [ ] **步骤 1：阅读测试质量约束**

在写测试前完整阅读 `C:/Users/zz/.codex/skills/test-driven-development/writing-good-tests.md`，确认测试断言真实配置行为，不以源代码文本匹配替代运行测试。

- [ ] **步骤 2：写出配置迁移失败测试**

新增数据驱动测试，明确覆盖新键优先、两个旧键的四种布尔组合、只有任一旧键、所有键缺失。测试辅助函数直接使用临时 INI：

```cpp
Configuration load( QSettings& settings )
{
    Configuration configuration;
    configuration.retrieveFromStorage( settings );
    return configuration;
}

void LineNumberConfigurationTest::migratesLegacyValues_data()
{
    QTest::addColumn<QVariant>( "main" );
    QTest::addColumn<QVariant>( "filtered" );
    QTest::addColumn<bool>( "expected" );
    QTest::newRow( "false-false" ) << QVariant::fromValue( false )
                                    << QVariant::fromValue( false ) << false;
    QTest::newRow( "false-true" ) << QVariant::fromValue( false )
                                   << QVariant::fromValue( true ) << true;
    QTest::newRow( "true-false" ) << QVariant::fromValue( true )
                                   << QVariant::fromValue( false ) << true;
    QTest::newRow( "true-true" ) << QVariant::fromValue( true )
                                  << QVariant::fromValue( true ) << true;
    QTest::newRow( "main-only" ) << QVariant::fromValue( false ) << QVariant{} << false;
    QTest::newRow( "filtered-only" ) << QVariant{} << QVariant::fromValue( true ) << true;
}
```

每个迁移断言必须同时验证：

```cpp
QCOMPARE( loaded.lineNumbersVisible(), expected );
QCOMPARE( settings.value( "view.lineNumbersVisible" ).toBool(), expected );
QVERIFY( !settings.contains( "view.lineNumbersVisibleInMain" ) );
QVERIFY( !settings.contains( "view.lineNumbersVisibleInFiltered" ) );
```

另加三个独立用例：无键时默认 `true`；新键存在时覆盖互相冲突的旧键；调用 setter 保存后只留下新键。

- [ ] **步骤 3：注册测试并确认 RED**

在 `tests/ui2/CMakeLists.txt` 注册：

```cmake
add_executable(zzlogg_line_number_configuration_test
  linenumberconfigurationtest.cpp)
target_link_libraries(zzlogg_line_number_configuration_test PRIVATE
  klogg_settings Qt6::Test Qt6::Core)
set_target_properties(zzlogg_line_number_configuration_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui2.line_number_configuration
  COMMAND zzlogg_line_number_configuration_test)
```

执行并确认因缺少 `lineNumbersVisible()` / `setLineNumbersVisible()` 而编译失败：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_line_number_configuration_test --parallel 8
```

- [ ] **步骤 4：实现单一字段和确定性迁移**

头文件改用单一存储字段和新 API：

```cpp
bool lineNumbersVisible() const { return lineNumbersVisible_; }
void setLineNumbersVisible( bool visible ) { lineNumbersVisible_ = visible; }
bool lineNumbersVisible_ = true;
```

为保证 Task 1 提交仍可完整编译，暂时保留四个旧方法作为直接映射到同一字段的兼容 wrapper：两个旧 getter 都返回 `lineNumbersVisible_`，两个旧 setter 都写 `lineNumbersVisible_`。不得保留两个旧字段。Task 2 更新所有 UI 调用点后删除这些 wrapper。

读取配置时按以下顺序实现，不依赖旧默认值：

```cpp
constexpr auto newKey = "view.lineNumbersVisible";
constexpr auto oldMainKey = "view.lineNumbersVisibleInMain";
constexpr auto oldFilteredKey = "view.lineNumbersVisibleInFiltered";

if ( settings.contains( newKey ) ) {
    lineNumbersVisible_ = settings.value( newKey ).toBool();
}
else {
    const bool hasMain = settings.contains( oldMainKey );
    const bool hasFiltered = settings.contains( oldFilteredKey );
    if ( hasMain && hasFiltered ) {
        lineNumbersVisible_ = settings.value( oldMainKey ).toBool()
                              || settings.value( oldFilteredKey ).toBool();
    }
    else if ( hasMain ) {
        lineNumbersVisible_ = settings.value( oldMainKey ).toBool();
    }
    else if ( hasFiltered ) {
        lineNumbersVisible_ = settings.value( oldFilteredKey ).toBool();
    }
    else {
        lineNumbersVisible_ = true;
    }
}
settings.setValue( newKey, lineNumbersVisible_ );
settings.remove( oldMainKey );
settings.remove( oldFilteredKey );
```

保存路径再次写新键并清理旧键，保证用户在未经历读取迁移的写入场景也能收敛配置。

- [ ] **步骤 5：确认 GREEN 并提交**

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_line_number_configuration_test --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.line_number_configuration$' --output-on-failure
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui-relwithdebinfo --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo --output-on-failure
git add src/settings/include/configuration.h src/settings/src/configuration.cpp tests/ui2/CMakeLists.txt tests/ui2/linenumberconfigurationtest.cpp
git commit -m "feat: unify line number configuration"
```

## Task 2：用一个 View 动作同步控制两种日志视图

**文件：**

- Create: `tests/ui2/linenumberuitest.cpp`
- Modify: `tests/ui2/CMakeLists.txt`
- Modify: `src/ui/include/mainwindowtext.h`
- Modify: `src/ui/src/mainwindowtext.cpp`
- Modify: `src/ui/include/mainwindow.h`
- Modify: `src/ui/src/mainwindow.cpp`
- Modify: `src/ui/include/abstractlogview.h`
- Modify: `src/ui/src/crawlerwidget.cpp`
- Modify: `src/app/i18n/en.ts`
- Modify: `src/app/i18n/zh_CN.ts`
- Modify: `src/app/i18n/zh_TW.ts`

- [ ] **步骤 1：写出单一动作和双视图同步的失败测试**

测试在隔离的 `StorageContext` 与临时日志文件下创建 `MainWindow`，加载文档后查找稳定对象名：

```cpp
auto* action = window.findChild<QAction*>( "lineNumbersVisibleAction" );
QVERIFY( action );
QCOMPARE( action->text(), QStringLiteral( "Line &numbers" ) );
QCOMPARE( window.findChildren<QAction*>( QRegularExpression{
              QStringLiteral( "lineNumbersVisible(InMain|InFiltered)Action" ) } ).size(), 0 );

auto* mainView = window.findChild<LogMainView*>( "logMainView" );
auto* filteredView = window.findChild<FilteredView*>( "logFilteredView" );
QTRY_VERIFY( mainView && filteredView );

action->setChecked( false );
QTRY_VERIFY( !mainView->lineNumbersVisible() );
QTRY_VERIFY( !filteredView->lineNumbersVisible() );
action->setChecked( true );
QTRY_VERIFY( mainView->lineNumbersVisible() );
QTRY_VERIFY( filteredView->lineNumbersVisible() );
```

关闭并新开文档后再断言两种视图继承统一值，避免只验证当前实例。

- [ ] **步骤 2：注册测试并确认 RED**

新增 `zzlogg_line_number_ui_test`，链接 `klogg_ui`、`klogg_storage`、`Qt6::Test`、`Qt6::Widgets`；CTest 使用 `-platform offscreen`，Windows 环境沿用 `application_translation` 测试的 Qt plugin、Qt Core 和 TBB PATH 配置。

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_line_number_ui_test --parallel 8
```

确认测试因单一 action 和只读状态接口尚不存在而失败。

- [ ] **步骤 3：收敛动作、槽函数和配置消费点**

动作文本改为：

```cpp
const char* action::lineNumbersVisibleText = QT_TR_NOOP( "Line &numbers" );
```

主窗口只创建一个动作：

```cpp
lineNumbersVisibleAction = new QAction( tr( action::lineNumbersVisibleText ), this );
lineNumbersVisibleAction->setObjectName( QStringLiteral( "lineNumbersVisibleAction" ) );
lineNumbersVisibleAction->setCheckable( true );
lineNumbersVisibleAction->setChecked( config.lineNumbersVisible() );
connect( lineNumbersVisibleAction, &QAction::toggled, this, [this]( bool visible ) {
    auto& config = Configuration::get();
    config.setLineNumbersVisible( visible );
    config.save();
    Q_EMIT optionsChanged();
} );
```

删除两个旧动作、两个旧槽和旧菜单项；所有调用点更新后，删除 Task 1 暂留的四个旧配置 wrapper。在 `CrawlerWidget::applyConfiguration()` 中对主视图及过滤视图调用同一个值：

```cpp
const bool visible = config.lineNumbersVisible();
logMainView_->setLineNumbersVisible( visible );
filteredView_->setLineNumbersVisible( visible );
```

`AbstractLogView` 增加无副作用查询：

```cpp
bool lineNumbersVisible() const { return lineNumbersVisible_; }
```

为两个视图设置测试也可使用的稳定 `objectName`，不暴露内部实现指针。

- [ ] **步骤 4：更新本任务翻译、确认 GREEN 并检查旧 API 已清除**

先更新翻译目录，把 `Line &numbers` 补成简体 `显示行号(&N)`、繁体 `顯示行號(&N)`，英文保持源文本：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target lupdate
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_line_number_ui_test --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.line_number_ui$' --output-on-failure
rg -n "mainLineNumbersVisible|filteredLineNumbersVisible|setMainLineNumbersVisible|setFilteredLineNumbersVisible|lineNumbersVisibleInMainAction|lineNumbersVisibleInFilteredAction" src tests
```

最后一条命令应无输出。

- [ ] **步骤 5：提交**

```powershell
git add src/settings src/ui src/app/i18n tests/ui2
git commit -m "feat: unify line number visibility action"
```

## Task 3：让设置窗口与存储位置页完整即时重译

**文件：**

- Modify: `tests/ui2/applicationtranslationtest.cpp`
- Modify: `src/ui/include/optionsdialog.h`
- Modify: `src/ui/src/optionsdialog.cpp`
- Modify: `src/ui/include/storagelocationpage.h`
- Modify: `src/ui/src/storagelocationpage.cpp`
- Modify: `src/app/i18n/en.ts`
- Modify: `src/app/i18n/zh_CN.ts`
- Modify: `src/app/i18n/zh_TW.ts`

- [ ] **步骤 1：为“先构造、后切换语言”写失败测试**

在测试进程启动时安装临时 `StorageContext`，先安装英文并构造 `OptionsDialog`，记录稳定数据和未保存编辑内容，再安装简体中文并处理事件：

```cpp
QCOMPARE( MainWindow::installLanguage( "en" ), 0 );
OptionsDialog dialog;
auto* theme = child<QComboBox>( dialog, "themeModeComboBox" );
auto* regexType = child<QComboBox>( dialog, "mainSearchBox" );
auto* encoding = child<QComboBox>( dialog, "encodingComboBox" );
auto* storagePage = child<StorageLocationPage>( dialog, "storageLocationPage" );
const QVariant selectedThemeData = theme->currentData();
const QVariant selectedRegexData = regexType->currentData();
const QVariant selectedEncodingData = encoding->currentData();

QPointer<KeySequencePresenter> editedShortcut = qobject_cast<KeySequencePresenter*>(
    child<QTableWidget>( dialog, "shortcutsTable" )->cellWidget( 0, 1 ) );
const QString unsavedShortcut = editedShortcut->keySequence();

QCOMPARE( MainWindow::installLanguage( "zh_CN" ), 0 );
QCoreApplication::sendPostedEvents();
QCoreApplication::processEvents();

QCOMPARE( theme->itemText( theme->findData( int( UiThemeMode::Light ) ) ),
          QStringLiteral( "浅色" ) );
QCOMPARE( theme->currentData(), selectedThemeData );
QCOMPARE( regexType->currentData(), selectedRegexData );
QCOMPARE( encoding->currentData(), selectedEncodingData );
QVERIFY( editedShortcut );
QCOMPARE( editedShortcut->keySequence(), unsavedShortcut );
QCOMPARE( child<QLabel>( *storagePage, "storageLocationExplanation" )->text(),
          QStringLiteral( "请选择 ZzLogg 数据的保存位置。" ) );
```

同时断言 Storage 标签、主搜索与 Quick Find 正则类型、正则引擎、Auto 编码和快捷键表头。快捷键动作名称的首次语言冻结由 Task 4 的专门 RED/GREEN 测试覆盖；Task 3 只保证表格不被重建。再从同一个存活窗口切到 `zh_TW`，覆盖第二次语言变化而不是重新构造控件。

- [ ] **步骤 2：写出“仅改语言不弹重启警告”的失败测试**

测试用 `QTimer` 观察活动模态窗口，若发现 `QMessageBox` 则记录并自动关闭，保证旧实现不会挂住测试：

```cpp
bool restartWarningSeen = false;
QTimer modalWatcher;
connect( &modalWatcher, &QTimer::timeout, [&] {
    if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
        restartWarningSeen = true;
        box->accept();
    }
} );
modalWatcher.start( 10 );

languageCombo->setCurrentIndex( languageCombo->findData( "zh_CN" ) );
QVERIFY( QMetaObject::invokeMethod( &dialog, "updateConfigFromDialog",
                                    Qt::DirectConnection ) );
QVERIFY( !restartWarningSeen );
QCOMPARE( restartSpy.count(), 0 );
```

旧实现应因观察到重启消息而 RED。

- [ ] **步骤 3：集中刷新 OptionsDialog 的动态项目**

新增私有方法：

```cpp
void OptionsDialog::retranslateDynamicUi()
{
    const auto replaceText = []( QComboBox* box, const QVariant& data,
                                 const QString& text ) {
        const int index = box->findData( data );
        if ( index >= 0 ) box->setItemText( index, text );
    };

    retranslateUi( this );
    setWindowTitle( tr( "%1 preferences" ).arg( QApplication::applicationDisplayName() ) );
    tabWidget->setTabText( tabWidget->indexOf( storageLocationPage_ ), tr( "Storage" ) );
    replaceText( themeModeComboBox, int( UiThemeMode::Light ), tr( "Light" ) );
    replaceText( themeModeComboBox, int( UiThemeMode::Dark ), tr( "Dark" ) );
    replaceText( mainSearchBox, int( SearchRegexpType::ExtendedRegexp ),
                 tr( "Extended Regexp" ) );
    replaceText( mainSearchBox, int( SearchRegexpType::FixedString ),
                 tr( "Fixed Strings" ) );
    replaceText( quickFindSearchBox, int( SearchRegexpType::ExtendedRegexp ),
                 tr( "Extended Regexp" ) );
    replaceText( quickFindSearchBox, int( SearchRegexpType::FixedString ),
                 tr( "Fixed Strings" ) );
    replaceText( regexpEngineComboBox, int( RegexpEngine::Hyperscan ), tr( "Hyperscan" ) );
    replaceText( regexpEngineComboBox, int( RegexpEngine::QRegularExpression ), tr( "Qt" ) );
    replaceText( encodingComboBox, -1, tr( "Auto" ) );
    retranslateShortcutTable(); // Task 3 更新表头且不重建 cell；Task 4 增加动作名称刷新。
    storageLocationPage_->retranslateUi();
}
```

`setupRegexp()` 改用 `addItem(text, int(enumValue))` 为主搜索、Quick Find 和引擎 combo 建立稳定数据；相应 getter 使用 `currentData()`，setter 使用 `findData()`。不要清空 combo box，不调用 `buildShortcutsTable()`。更新前后索引和数据均不变。构造期裸字符串全部改成 `tr()`。

在 `OptionsDialog::changeEvent()` 响应 `QEvent::LanguageChange`，调用集中函数并继续交给基类。`updateConfigFromDialog()` 安装新翻译后同样调用该函数，并删除：

```cpp
restartAppMessage |= config.language() != languageComboBox->currentData().toString();
```

旧样式与存储迁移原有重启判定保持不动。

- [ ] **步骤 4：让 StorageLocationPage 拥有并刷新全部文本**

把 explanation label 和 `QFormLayout` 的“Data directory”标签变成成员；在头文件 public 区声明 `retranslateUi()`，在 protected 区覆盖 `changeEvent(QEvent*)`，实现：

```cpp
void StorageLocationPage::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::LanguageChange ) retranslateUi();
    QWidget::changeEvent( event );
}

void StorageLocationPage::retranslateUi()
{
    explanationLabel_->setText( tr( "请选择 ZzLogg 数据的保存位置。" ) );
    userStorageRadio_->setText( tr( "用户数据目录" ) );
    programStorageRadio_->setText( tr( "程序目录（data）" ) );
    customStorageRadio_->setText( tr( "自定义目录" ) );
    customStoragePath_->setPlaceholderText( tr( "请输入绝对路径" ) );
    dataDirectoryLabel_->setText( tr( "数据目录：" ) );
    browseStorageButton_->setText( tr( "浏览…" ) );
    openStorageDirectoryButton_->setText( tr( "打开目录" ) );
    refreshValidation();
}
```

该函数不修改 `mode_`、路径文本、`commandLineManaged_` 或选择状态。

- [ ] **步骤 5：更新本任务翻译并运行翻译测试**

先运行 `lupdate`，补齐本任务涉及的 Storage 标签、Light/Dark、正则类型/引擎、Auto 编码、快捷键表头、设置对话框动态文案的简体和繁体翻译；快捷键动作名称留给 Task 4。英文保持源文本。随后构建测试资源：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target lupdate
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_application_translation_test --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.application_translation$' --output-on-failure
```

- [ ] **步骤 6：提交**

```powershell
git add src/ui/include/optionsdialog.h src/ui/src/optionsdialog.cpp src/ui/include/storagelocationpage.h src/ui/src/storagelocationpage.cpp src/app/i18n tests/ui2/applicationtranslationtest.cpp
git commit -m "fix: refresh settings language at runtime"
```

## Task 4：消除快捷键名称的首次语言冻结

**文件：**

- Modify: `tests/ui2/applicationtranslationtest.cpp`
- Modify: `src/settings/include/shortcuts.h`
- Modify: `src/settings/src/shortcuts.cpp`
- Modify: `src/ui/src/optionsdialog.cpp`
- Modify: `src/app/i18n/en.ts`
- Modify: `src/app/i18n/zh_CN.ts`
- Modify: `src/app/i18n/zh_TW.ts`

- [ ] **步骤 1：添加先预热英文、再切中文的失败测试**

在构造设置窗口前先调用两次快捷键 API，确保测试能暴露函数级静态初始化问题：

```cpp
QCOMPARE( MainWindow::installLanguage( "en" ), 0 );
const auto& definitions = ShortcutAction::defaultShortcutList();
QCOMPARE( ShortcutAction::displayName( ShortcutAction::MainWindowOpenFile ),
          QStringLiteral( "Open file" ) );

OptionsDialog dialog;
QCOMPARE( MainWindow::installLanguage( "zh_CN" ), 0 );
QCoreApplication::processEvents();
QCOMPARE( ShortcutAction::displayName( ShortcutAction::MainWindowOpenFile ),
          QStringLiteral( "打开文件" ) );
```

再定位表格中 `Qt::UserRole == MainWindowOpenFile` 的行，断言名称已变而两个 `KeySequencePresenter` 对象地址与键值保持不变。

- [ ] **步骤 2：确认 RED**

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_application_translation_test --parallel 8
```

预期失败点是 `displayName` 尚不存在；如果临时只加入声明，现有静态翻译仍返回英文。

- [ ] **步骤 3：将静态定义改为源文本，展示时才翻译**

快捷键描述存储不可变源文本和默认键：

```cpp
struct ShortcutDesc {
    const char* sourceText;
    QStringList keySequence;
};

static QString displayName( const std::string& action );
```

列表使用 `QT_TRANSLATE_NOOP` 标记可提取文本：

```cpp
{ MainWindowOpenFile,
  { QT_TRANSLATE_NOOP( "ShortcutAction", "Open file" ),
    getKeyBindings( QKeySequence::Open ) } },
```

每次展示时才求值：

```cpp
QString ShortcutAction::displayName( const std::string& action )
{
    const auto& definition = defaultShortcutList().at( action );
    return QCoreApplication::translate( "ShortcutAction", definition.sourceText );
}
```

`OptionsDialog::retranslateShortcutTable()` 遍历现有行，从 `Qt::UserRole` 取稳定 action key，仅更新第 0 列名称与表头；不得重建 cell widget。

- [ ] **步骤 4：更新本任务翻译、确认 GREEN 并提交**

运行 `lupdate` 后，把迁移到 `ShortcutAction` 上下文的动作名称补齐英文、简体和繁体翻译，再构建测试资源：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target lupdate
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_application_translation_test --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.application_translation$' --output-on-failure
git add src/settings/include/shortcuts.h src/settings/src/shortcuts.cpp src/ui/src/optionsdialog.cpp src/app/i18n tests/ui2/applicationtranslationtest.cpp
git commit -m "fix: translate shortcut names on demand"
```

## Task 5：刷新已打开文档的搜索区与 Quick Find

**文件：**

- Modify: `tests/ui2/applicationtranslationtest.cpp`
- Modify: `src/ui/include/crawlerwidget.h`
- Modify: `src/ui/src/crawlerwidget.cpp`
- Modify: `src/ui/include/predefinedfilterscombobox.h`
- Modify: `src/ui/src/predefinedfilterscombobox.cpp`
- Modify: `src/ui/include/quickfindwidget.h`
- Modify: `src/ui/src/quickfindwidget.cpp`
- Modify: `src/ui/include/qfnotifications.h`
- Modify: `src/app/i18n/en.ts`
- Modify: `src/app/i18n/zh_CN.ts`
- Modify: `src/app/i18n/zh_TW.ts`

- [ ] **步骤 1：为存活的文档控件写失败测试**

先用英文加载临时日志并等待 crawler 就绪，取得按钮、复选框、预定义过滤器、Quick Find 控件和搜索历史 action 的地址。切到简中后断言：

```cpp
QCOMPARE( child<QToolButton>( *crawler, "mainSearchButton" )->text(),
          QStringLiteral( "搜索" ) );
QCOMPARE( child<QToolButton>( *crawler, "clearSearchButton" )->text(),
          QStringLiteral( "清除搜索文本" ) );
QCOMPARE( child<QToolButton>( *crawler, "matchCaseButton" )->toolTip(),
          QStringLiteral( "匹配大小写" ) );
QCOMPARE( child<PredefinedFiltersComboBox>( *crawler, "predefinedFilters" )->itemText( 0 ),
          QStringLiteral( "预定义过滤器" ) );
QCOMPARE( child<QCheckBox>( *quickFind, "ignoreCaseCheckBox" )->text(),
          QStringLiteral( "忽略大小写(&C)" ) );
QCOMPARE( child<QToolButton>( *quickFind, "previousButton" )->text(),
          QStringLiteral( "上一个" ) );
QCOMPARE( child<QToolButton>( *quickFind, "nextButton" )->text(),
          QStringLiteral( "下一个" ) );
```

再切换繁中并断言同一批控件地址未变、搜索文本和当前结果数未变。触发一次无匹配 Quick Find，断言通知文字使用当前语言。

- [ ] **步骤 2：确认 RED**

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_application_translation_test --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.application_translation$' --output-on-failure
```

- [ ] **步骤 3：为 CrawlerWidget 集中重译动态搜索 UI**

把构造时创建的搜索历史 action 保存为成员并设置 object name。新增：

```cpp
void CrawlerWidget::retranslateUi()
{
    visibilityBox_->setItemText( 0, tr( "Marks and matches" ) );
    visibilityBox_->setItemText( 1, tr( "Marks" ) );
    visibilityBox_->setItemText( 2, tr( "Matches" ) );
    matchCaseButton_->setToolTip( tr( "Match case" ) );
    useRegexpButton_->setToolTip( tr( "Use regex" ) );
    inverseButton_->setToolTip( tr( "Inverse match" ) );
    booleanButton_->setToolTip( tr( "Enable regular expression logical combining" ) );
    searchRefreshButton_->setToolTip( tr( "Auto-refresh" ) );
    clearSearchHistoryAction_->setText( tr( "Clear search history" ) );
    editSearchHistoryAction_->setText( tr( "Edit search history" ) );
    saveAsPredefinedFilterAction_->setText( tr( "Save as Filter" ) );
    searchButton_->setText( tr( "Search" ) );
    clearButton_->setText( tr( "Clear search text" ) );
    predefinedFilters_->retranslateUi();
    printSearchInfoMessage( nbMatches_ );
    updateEncodingText();
}
```

现有 `changeEvent()` 在主题分支外增加 `QEvent::LanguageChange`。`updateEncodingText()` 只根据当前 codec/MIB 更新标签，禁止调用会改变编码、重载文件或中断加载的 `updateEncoding()`。

- [ ] **步骤 4：为子控件增加自身语言变化处理**

`PredefinedFiltersComboBox` 将 `retranslateUi()` 设为 public，供 crawler 主动刷新，并在 protected 区覆盖 `changeEvent()`；`QuickFindWidget` 同样增加私有重译方法和 protected `changeEvent()`。Quick Find 的三个裸英文控件文字全部改为 `tr()`；`qfnotifications.h` 中静态已翻译字符串改成按需返回函数：

```cpp
static QString reachedEndOfFileText()
{
    return QCoreApplication::translate(
        "QFNotification", "Reached end of file, no occurrence found." );
}

static QString reachedBeginningOfFileText()
{
    return QCoreApplication::translate(
        "QFNotification", "Reached beginning of file, no occurrence found." );
}

static QString interruptedText()
{
    return QCoreApplication::translate( "QFNotification", "Search interrupted" );
}
```

这样通知生成时总使用当前 translator，不保留首次调用时的旧文本。

- [ ] **步骤 5：更新本任务翻译、确认 GREEN 并提交**

运行 `lupdate` 后，补齐本任务新增或从裸字符串转为 `tr()` 的 Crawler、Predefined Filters、Quick Find 与通知文案翻译，再构建测试资源：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target lupdate
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_application_translation_test --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.application_translation$' --output-on-failure
git add src/ui/include/crawlerwidget.h src/ui/src/crawlerwidget.cpp src/ui/include/predefinedfilterscombobox.h src/ui/src/predefinedfilterscombobox.cpp src/ui/include/quickfindwidget.h src/ui/src/quickfindwidget.cpp src/ui/include/qfnotifications.h src/app/i18n tests/ui2/applicationtranslationtest.cpp
git commit -m "fix: retranslate open document controls"
```

## Task 6：补齐主窗口残留菜单、托盘、标题和状态文本

**文件：**

- Modify: `tests/ui2/applicationtranslationtest.cpp`
- Modify: `src/ui/include/mainwindow.h`
- Modify: `src/ui/src/mainwindow.cpp`
- Modify: `src/ui/include/encodings.h`
- Modify: `src/app/i18n/en.ts`
- Modify: `src/app/i18n/zh_CN.ts`
- Modify: `src/app/i18n/zh_TW.ts`

- [ ] **步骤 1：写出主窗口残留项失败测试**

在英文下构造带一个已打开文档的 `MainWindow`，通过稳定 object name 取得 Recent、Encoding、托盘 Open/Quit action；触发显示暂存器后从 `QApplication::topLevelWidgets()` 定位 `TabbedScratchPad`。安装简中后处理 `LanguageChange`，断言：

```cpp
QCOMPARE( recentFilesMenu->title(), QStringLiteral( "最近打开文件" ) );
QCOMPARE( encodingMenu->title(), QStringLiteral( "编码" ) );
QCOMPARE( encodingAutoAction->text(), QStringLiteral( "自动" ) );
QCOMPARE( trayOpenAction->text(), QStringLiteral( "打开窗口" ) );
QCOMPARE( trayQuitAction->text(), QStringLiteral( "退出" ) );
QCOMPARE( scratchPad->windowTitle(), QStringLiteral( "ZzLogg - 暂存器" ) );
QVERIFY( window.windowTitle().contains( QStringLiteral( "ZzLogg" ) ) );
```

记录当前文件、当前 tab、编码 action 勾选状态、窗口标题中的文件名和搜索文本；切换繁中后断言业务状态不变。

- [ ] **步骤 2：确认 RED**

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_application_translation_test --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.application_translation$' --output-on-failure
```

- [ ] **步骤 3：保存菜单/托盘动作并扩充 MainWindow::reTranslateUI()**

复用已经是成员的 `recentFilesMenu`，将其补上稳定 object name；新增成员并设置 object name：`encodingMenu`、`trayOpenAction`、`trayQuitAction`。重译函数增加：

```cpp
recentFilesMenu->setTitle( tr( "Open Recent" ) );
EncodingMenu::retranslate( encodingMenu );
trayOpenAction->setText( tr( "Open window" ) );
trayQuitAction->setText( tr( "Quit" ) );
scratchPad_.setWindowTitle( tr( "%1 - scratchpad" ).arg( productName() ) );
auto* crawler = currentCrawlerWidget();
updateTitleBar( crawler ? session_.getFilename( crawler ) : QString{} );
if ( crawler ) updateInfoLine();
```

`updateTitleBar()` 从当前 session 读取现有文件名，不重载文件；`updateInfoLine()` 只重新格式化当前文件、大小、编码和修改时间，不调用搜索或改变选区。

- [ ] **步骤 4：让编码菜单可原位重译**

`EncodingMenu::generate()` 为顶层菜单、Auto、System 和各稳定 action 设置 object name/data。新增：

```cpp
static void retranslate( QMenu* encodingsMenu )
{
    using namespace klogg::mainwindow;
    encodingsMenu->setTitle( QApplication::translate( "klogg::mainwindow::menu",
                                                      menu::encodingTitle ) );
    encodingsMenu->findChild<QAction*>( "encodingAutoAction" )
        ->setText( QApplication::translate( "klogg::mainwindow::action",
                                            action::autoEncodingText ) );
    encodingsMenu->findChild<QAction*>( "encodingSystemAction" )
        ->setText( QCoreApplication::translate( "EncodingMenu", "System (%1)" )
                       .arg( encodingsMenu->findChild<QAction*>( "encodingSystemAction" )
                                 ->property( "encodingName" ).toString() ) );
}
```

`generate()` 在创建 system action 时把 codec 名存入 `encodingName` property。`retranslate()` 只修改显示文本，不清空 menu，不创建新的 QAction，不改变 `QActionGroup::checkedAction()`。

- [ ] **步骤 5：更新本任务翻译、确认 GREEN 并提交**

运行 `lupdate` 后，补齐 Open Recent、Encoding、Auto、System、托盘 Open/Quit、scratchpad 及当前标题/状态文案的简体和繁体翻译，再构建测试资源：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target lupdate
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_application_translation_test --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.application_translation$' --output-on-failure
git add src/ui/include/mainwindow.h src/ui/src/mainwindow.cpp src/ui/include/encodings.h src/app/i18n tests/ui2/applicationtranslationtest.cpp
git commit -m "fix: complete main window runtime translation"
```

## Task 7：更新翻译目录并做全量回归验证

**文件：**

- Modify: `src/app/i18n/en.ts`
- Modify: `src/app/i18n/zh_CN.ts`
- Modify: `src/app/i18n/zh_TW.ts`

- [ ] **步骤 1：运行最终 lupdate 并检查目录差异**

各功能任务已经同步补齐自己的翻译。再次运行项目的 `lupdate` 目标，确认没有遗漏的新增源文本：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target lupdate
git diff -- src/app/i18n/en.ts src/app/i18n/zh_CN.ts src/app/i18n/zh_TW.ts
```

检查 diff 中本次新增或移动上下文的条目；如果 `lupdate` 新产生条目，立即补齐英文、简体和繁体翻译。旧的两个行号动作不再作为有效消息存在。

本次至少确保下列映射是 finished 状态：

| Source | zh_CN | zh_TW |
|---|---|---|
| `Line &numbers` | `显示行号(&N)` | `顯示行號(&N)` |
| `Light` | `浅色` | `淺色` |
| `Dark` | `深色` | `深色` |
| `Auto` | `自动` | `自動` |
| `Previous` | `上一个` | `上一個` |
| `Next` | `下一个` | `下一個` |
| `Ignore &case` | `忽略大小写(&C)` | `忽略大小寫(&C)` |

所有 Task 3–6 新增的源文本均在简体和繁体 TS 中有非空翻译；英文 TS 保留源文本。

- [ ] **步骤 2：重建翻译资源并运行行为测试集合**

实际构建 QM 并运行存活控件的语言切换测试，以用户可观察行为验证翻译，而不是匹配 TS 或 C++ 源码文本：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build out/ui-vs --config RelWithDebInfo --target zzlogg_application_translation_test zzlogg_line_number_configuration_test zzlogg_line_number_ui_test --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo -R '^zzlogg_ui2\.(application_translation|application_translation_contract|line_number_configuration|line_number_ui|storage_location_ui|options_theme)$' --output-on-failure
```

- [ ] **步骤 3：提交最终目录整理**

```powershell
git add src/app/i18n
git commit -m "i18n: complete runtime UI translations"
```

- [ ] **步骤 4：运行全量构建与 CTest**

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui-relwithdebinfo --parallel 8
& 'D:\SoftWare\CMake\bin\ctest.exe' --test-dir out/ui-vs -C RelWithDebInfo --output-on-failure
```

预期所有目标编译成功，CTest 退出码为 0。若新增测试后总数高于基线 53，以实际测试数为准并记录通过数。

- [ ] **步骤 5：检查变更边界与提交历史**

```powershell
git status --short
git diff ddceeec5..HEAD --check
git log --oneline --decorate ddceeec5..HEAD
rg -n "view\.lineNumbersVisibleInMain|view\.lineNumbersVisibleInFiltered" src tests
```

最后一条只允许出现在迁移测试和迁移实现中；工作树应干净。确认没有修改主工作区中的用户文件 `serach.png`、`serach.svg`。

## 完成标准

- 英文下构造的设置窗口、主窗口、已打开文档搜索区和 Quick Find 在切换简体/繁体后立即更新，不需要重启。
- 重译不丢失设置页未保存快捷键、combo box 选择、当前文件、搜索状态、编码勾选、tab 和选区。
- 仅修改语言不弹出重启提示；旧样式与存储位置原有重启规则不变。
- View 菜单只有一个“显示行号”动作，同时控制主视图与过滤视图，新文档同样遵循该设置。
- 新键优先，旧键迁移规则与设计文档一致，旧键被清理，新安装默认开启。
- 三份翻译资源、定向测试、完整构建和全量 CTest 全部通过。
