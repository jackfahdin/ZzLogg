# 统一存储与单 GUI 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将 UI2 变成唯一正式的 `ZzLogg.exe`，并让同一可执行文件在首次启动和设置中选择用户目录、程序目录或自定义目录，统一保存配置、会话、日志和崩溃数据。

**架构：** 新增独立 `klogg_storage` 库，提供不可变 `StorageContext`、locator、目录校验、旧数据检测和事务迁移；应用必须在任何 `Persistable` 初始化前安装一次 StorageContext。GUI 用复用的存储位置页面完成首次选择和后续迁移调度，设置切换在重启时执行；Logger、CrashHandler 和 PersistentInfo 只向 StorageContext 请求路径。

**技术栈：** C++17/C++20、Qt 6.11 Core/Widgets/Test、QSettings/QSaveFile/QLockFile、CMake 4、CTest、MSVC 2026、ZzPureTools UI2。

---

## 强制执行顺序

任务编号按子系统说明排列，但启动依赖要求实际执行顺序为：`1 → 2 → 3 → 6 → 7 → 4 → 5 → 8 → 9 → 10`。必须先完成可复用选择 UI 和启动 bootstrap，再让 PersistentInfo 强制要求已安装的 StorageContext；这样每一个中间提交都保留可启动的 GUI。

## 文件结构

### 新增存储核心

- 创建 `src/storage/CMakeLists.txt`：定义 `klogg_storage` 静态库。
- 创建 `src/storage/include/storagecontext.h`、`src/storage/src/storagecontext.cpp`：模式、路径布局和进程级只安装一次的 StorageContext。
- 创建 `src/storage/include/storagevalidator.h`、`src/storage/src/storagevalidator.cpp`：路径规范化、可写性探测和 manifest。
- 创建 `src/storage/include/storagelocator.h`、`src/storage/src/storagelocator.cpp`：相邻/用户 locator 的读取、优先级和原子写入。
- 创建 `src/storage/include/legacystorage.h`、`src/storage/src/legacystorage.cpp`：旧普通版/便携版数据检测。
- 创建 `src/storage/include/storagemigrator.h`、`src/storage/src/storagemigrator.cpp`：复制、验证、提交和回滚迁移事务。

### 新增 GUI 与启动协调

- 创建 `src/ui/include/storagelocationpage.h`、`src/ui/src/storagelocationpage.cpp`：三种模式、路径预览、浏览和验证结果。
- 创建 `src/ui/include/storagebootstrapdialog.h`、`src/ui/src/storagebootstrapdialog.cpp`：首次启动和目录恢复对话框。
- 创建 `src/app/storagebootstrap.h`、`src/app/storagebootstrap.cpp`：连接 CLI、locator、旧数据、迁移器和 GUI 选择器。

### 修改持久化消费者

- 修改 `src/settings/include/persistentinfo.h`、`src/settings/src/persistentinfo.cpp`：删除 ForcePortable 和自主路径探测，直接打开 StorageContext 的两个 INI。
- 修改 `src/logging/include/logger.h`、`src/logging/src/logger.cpp`：运行日志写入 `logs/`。
- 修改 `src/crash_handler/include/crashhandler.h`、`src/crash_handler/src/crashhandler.cpp`：崩溃数据库写入 `crashes/`。
- 修改 `src/ui/include/optionsdialog.h`、`src/ui/src/optionsdialog.cpp`：新增存储设置页和迁移调度。
- 修改 `src/ui/include/mainwindow.h`、`src/ui/src/mainwindow.cpp`、`src/app/kloggapp.h`：实现安全重启请求。
- 修改 `src/app/cli.h`、`src/app/applicationrunner.cpp`、`src/app/main.cpp`、`src/app/klogg_grep.cpp`：在配置初始化前完成路径解析。

### 修改构建、测试与发布

- 修改 `src/CMakeLists.txt` 及各消费者的 `CMakeLists.txt`：链接 `klogg_storage`。
- 修改根 `CMakeLists.txt`、`cmake/ZzPureTools.cmake`、`src/ui2/CMakeLists.txt`、`src/app/CMakeLists.txt`、`cmake/prepare_version.cmake`：UI2 成为唯一 GUI。
- 修改 `CMakePresets.json`、`CMakeUserPresets.json.example`：默认 Qt6 构建即为正式 UI，保留一版旧 UI2 preset 名称作为兼容别名。
- 修改 `.github/actions/agent-package-win/action.yml`：部署同一个 `ZzLogg.exe`。
- 修改 `tests/ui2/CMakeLists.txt` 及契约/烟雾脚本：使用 `klogg` 目标和显式测试数据根目录。
- 创建存储核心、迁移、消费者、启动和 GUI 测试文件。
- 修改 `src/app/i18n/en.ts`、`zh_CN.ts`、`zh_TW.ts` 与 `docs/BUILD.md`。

## 任务 1：建立 StorageContext 和固定目录布局

**文件：**
- 创建：`src/storage/CMakeLists.txt`
- 创建：`src/storage/include/storagecontext.h`
- 创建：`src/storage/src/storagecontext.cpp`
- 创建：`tests/ui2/storagecontexttest.cpp`
- 修改：`src/CMakeLists.txt`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：编写 StorageContext 失败测试**

测试必须覆盖三种模式、规范路径、固定子目录和进程只安装一次：

```cpp
class StorageContextTest final : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void exposesStableLayout();
    void installsOnlyOnce();
};

void StorageContextTest::exposesStableLayout()
{
    const StorageLocation location{ StorageMode::CustomDirectory,
                                    QStringLiteral( "C:/data/ZzLogg" ),
                                    QStringLiteral( "C:/bootstrap/storage.ini" ), false };
    const StorageContext context{ location };
    QCOMPARE( context.configFilePath(), QStringLiteral( "C:/data/ZzLogg/config/ZzLogg.ini" ) );
    QCOMPARE( context.sessionFilePath(),
              QStringLiteral( "C:/data/ZzLogg/session/ZzLogg_session.ini" ) );
    QCOMPARE( context.logsDirectory(), QStringLiteral( "C:/data/ZzLogg/logs" ) );
    QCOMPARE( context.crashesDirectory(), QStringLiteral( "C:/data/ZzLogg/crashes" ) );
    QCOMPARE( context.manifestFilePath(),
              QStringLiteral( "C:/data/ZzLogg/storage-manifest.ini" ) );
}

void StorageContextTest::installsOnlyOnce()
{
    QTemporaryDir root;
    QVERIFY( root.isValid() );
    QString error;
    QVERIFY( StorageContext::install(
        { StorageMode::UserDirectory, root.path(), root.filePath( "storage.ini" ), false },
        &error ) );
    QVERIFY( StorageContext::isInstalled() );
    QVERIFY( !StorageContext::install(
        { StorageMode::CustomDirectory, root.filePath( "other" ),
          root.filePath( "other.ini" ), false }, &error ) );
    QVERIFY( error.contains( "already installed" ) );
}
```

注册 `zzlogg_storage_context_test`，仅链接 `klogg_storage Qt6::Test Qt6::Core`。

- [ ] **步骤 2：运行测试确认链接失败**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_storage_context_test --parallel 8
```

预期：构建失败并报告 `storagecontext.h` 或 `klogg_storage` 不存在。

- [ ] **步骤 3：实现最小 StorageContext**

在 `storagecontext.h` 固定公开类型和 API，后续任务不得改名：

```cpp
enum class StorageMode { UserDirectory, ProgramDirectory, CustomDirectory };

struct StorageLocation {
    StorageMode mode = StorageMode::UserDirectory;
    QString dataRoot;
    QString locatorPath;
    bool commandLineOverride = false;
};

class StorageContext final {
  public:
    explicit StorageContext( StorageLocation location );
    static bool install( StorageLocation location, QString* error = nullptr );
    static bool isInstalled();
    static const StorageContext& current();

    const StorageLocation& location() const;
    QString dataRoot() const;
    QString configDirectory() const;
    QString sessionDirectory() const;
    QString logsDirectory() const;
    QString crashesDirectory() const;
    QString configFilePath() const;
    QString sessionFilePath() const;
    QString manifestFilePath() const;
    bool ensureDirectories( QString* error = nullptr ) const;
};
```

实现使用 `QDir::cleanPath(QDir::fromNativeSeparators(...))`，返回路径统一用 `/`，实际文件 API 可直接接受。`ensureDirectories()` 依次创建根、config、session、logs、crashes，任一失败返回包含具体目录的错误。

- [ ] **步骤 4：运行 StorageContext 测试**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_storage_context_test --parallel 8
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.storage_context$"
```

预期：测试通过。

- [ ] **步骤 5：提交 StorageContext**

```powershell
git add src/storage src/CMakeLists.txt tests/ui2/storagecontexttest.cpp tests/ui2/CMakeLists.txt
git commit -m "feat: add unified storage context"
```

## 任务 2：实现目录校验、manifest 和 locator 优先级

**文件：**
- 创建：`src/storage/include/storagevalidator.h`
- 创建：`src/storage/src/storagevalidator.cpp`
- 创建：`src/storage/include/storagelocator.h`
- 创建：`src/storage/src/storagelocator.cpp`
- 创建：`tests/ui2/storagevalidatortest.cpp`
- 创建：`tests/ui2/storagelocatortest.cpp`
- 修改：`src/storage/CMakeLists.txt`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：编写校验与 locator 失败测试**

固定接口：

```cpp
struct StorageValidationResult {
    bool valid = false;
    bool managedDirectory = false;
    QString normalizedRoot;
    QString error;
};

class StorageValidator final {
  public:
    static StorageValidationResult validate( const QString& root,
                                             bool allowExistingManagedDirectory );
    static bool writeManifest( const StorageContext& context, QString* error = nullptr );
    static bool hasCompatibleManifest( const QString& root );
};

enum class StorageResolutionSource {
    CommandLine, ProgramLocator, UserLocator, LegacyStorage, Missing
};

struct StorageMigrationRequest {
    QString transactionId;
    StorageLocation source;
    StorageLocation target;
    QString legacyConfigFile;
    QString legacySessionFile;
    QString legacyCrashDirectory;
};

struct StorageLocatorState {
    int formatVersion = 1;
    StorageLocation active;
    std::optional<StorageMigrationRequest> pending;
    bool verified = false;
};

struct StorageResolution {
    StorageResolutionSource source = StorageResolutionSource::Missing;
    std::optional<StorageLocatorState> state;
    QString error;
};

class StorageLocatorStore final {
  public:
    StorageLocatorStore( QString applicationDirectory, QString appConfigDirectory );
    QString programLocatorPath() const;
    QString userLocatorPath() const;
    StorageResolution resolve( const QString& commandLineDataRoot = {} ) const;
    bool writeActive( const StorageLocation& location, QString* error = nullptr ) const;
    bool writePending( const StorageMigrationRequest& request, QString* error = nullptr ) const;
    bool commitPending( const StorageMigrationRequest& request, QString* error = nullptr ) const;
    bool rollbackPending( const StorageMigrationRequest& request, QString* error = nullptr ) const;
};
```

测试至少断言：

```cpp
QCOMPARE( store.resolve( cliRoot ).source, StorageResolutionSource::CommandLine );
QCOMPARE( store.resolve().source, StorageResolutionSource::ProgramLocator );
QCOMPARE( store.resolve().state->active.dataRoot, programRoot );
```

并覆盖：CLI > 相邻 locator > 用户 locator；损坏 locator 返回错误而不是 Missing；空目录通过；普通非空目录被拒绝；带 layoutVersion=1 manifest 的目录在允许时通过；程序目录 locator 中相对 `data` 按 applicationDirectory 解析。

- [ ] **步骤 2：运行测试确认新 API 不存在**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_storage_validator_test zzlogg_storage_locator_test --parallel 8
```

预期：构建失败，报告新头文件或类型不存在。

- [ ] **步骤 3：实现原子校验和 manifest**

`validate()` 必须执行实际创建、写入、flush、读回和 `QSaveFile::commit()`，探测文件名使用 `.zzlogg-write-test-<uuid>` 并在所有返回路径清理。manifest 使用 IniFormat：

```ini
[Storage]
layoutVersion=1
product=ZzLogg
```

非空目录的判断忽略探测文件本身；没有兼容 manifest 时返回 `managedDirectory=false` 和包含目标绝对路径的错误。

- [ ] **步骤 4：实现 locator 原子读写与优先级**

locator 固定键：

```ini
[Storage]
formatVersion=1
mode=user|program|custom
dataRoot=<absolute path or program-relative data>
verified=true|false

[Pending]
transactionId=<uuid>
sourceMode=...
sourceRoot=...
sourceLocator=...
targetMode=...
targetRoot=...
targetLocator=...
legacyConfigFile=...
legacySessionFile=...
legacyCrashDirectory=...
```

写入使用同目录 `QSaveFile`。`writeActive()` 先成功写目标 locator，再删除相互冲突的旧 locator；删除失败时恢复目标 locator 的旧内容并返回失败。命令行覆盖创建 `commandLineOverride=true` 的临时 StorageLocation，不写 locator。

- [ ] **步骤 5：运行聚焦测试**

```powershell
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.(storage_validator|storage_locator)$"
```

预期：两组测试通过。

- [ ] **步骤 6：提交 locator 与校验器**

```powershell
git add src/storage tests/ui2/storagevalidatortest.cpp tests/ui2/storagelocatortest.cpp tests/ui2/CMakeLists.txt
git commit -m "feat: resolve and validate storage locations"
```

## 任务 3：实现旧数据检测和可回滚迁移

**文件：**
- 创建：`src/storage/include/legacystorage.h`
- 创建：`src/storage/src/legacystorage.cpp`
- 创建：`src/storage/include/storagemigrator.h`
- 创建：`src/storage/src/storagemigrator.cpp`
- 创建：`tests/ui2/legacystoragetest.cpp`
- 创建：`tests/ui2/storagemigratortest.cpp`
- 修改：`src/storage/CMakeLists.txt`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：编写旧数据优先级和迁移失败测试**

固定接口：

```cpp
struct LegacyStorage {
    StorageMode mode;
    QString configFile;
    QString sessionFile;
    QString crashDirectory;
};

class LegacyStorageDetector final {
  public:
    static std::optional<LegacyStorage> detect( const QString& applicationDirectory,
                                                const QString& userSettingsDirectory,
                                                const QString& oldCrashDirectory );
};

struct StorageMigrationResult {
    bool success = false;
    bool rolledBack = false;
    QString error;
};

using StorageCopyOperation
    = std::function<bool( const QString& source, const QString& target, QString* error )>;

class StorageMigrator final {
  public:
    explicit StorageMigrator( StorageLocatorStore locatorStore,
                              StorageCopyOperation copyOperation = {} );
    StorageMigrationResult execute( const StorageMigrationRequest& request ) const;
    StorageMigrationResult recoverPending( const StorageMigrationRequest& request ) const;
};
```

测试建立普通旧 INI 与相邻 `.conf` 两套来源，断言相邻便携来源优先。迁移成功测试断言设置、会话、crashes 和 manifest 均到目标，locator 指向目标且源文件仍存在。失败测试注入在复制 session 文件时返回 false，断言 locator 仍指向源、目标没有 manifest、`rolledBack=true`。

- [ ] **步骤 2：运行测试确认失败**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_legacy_storage_test zzlogg_storage_migrator_test --parallel 8
```

预期：构建失败，报告 detector/migrator 类型不存在。

- [ ] **步骤 3：实现旧数据检测**

便携来源文件固定为 `<applicationDir>/ZzLogg.conf` 与 `ZzLogg_session.conf`；普通来源固定为 `<userSettingsDir>/ZzLogg.ini` 与 `ZzLogg_session.ini`。相邻配置存在即返回 ProgramDirectory；否则普通配置或会话任一存在返回 UserDirectory；均不存在返回 `std::nullopt`。

- [ ] **步骤 4：实现带锁迁移**

`execute()` 按固定顺序执行：

```cpp
QLockFile lock{ request.source.locatorPath + QStringLiteral( ".migration.lock" ) };
lock.setStaleLockTime( 0 );
// tryLock(0) -> writePending -> ensure target dirs -> copy config/session/tree
// -> open target QSettings and check status -> writeManifest
// -> commitPending -> return success
```

设置和会话复制使用默认 `QSaveFile` copyOperation；目录递归复制拒绝符号链接，并在文件失败时返回具体源/目标。提交点前的失败调用 `rollbackPending()` 并删除本事务创建、但未写入 manifest 的目标文件；源文件不删除。`recoverPending()` 检查目标 manifest 和两个 INI：完整则重新 commit，缺失则 rollback。

- [ ] **步骤 5：运行迁移测试**

```powershell
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.(legacy_storage|storage_migrator)$"
```

预期：成功、故障注入和恢复幂等性用例全部通过。

- [ ] **步骤 6：提交迁移器**

```powershell
git add src/storage tests/ui2/legacystoragetest.cpp tests/ui2/storagemigratortest.cpp tests/ui2/CMakeLists.txt
git commit -m "feat: migrate legacy storage transactionally"
```

## 任务 4：让 PersistentInfo 完全使用 StorageContext

**文件：**
- 修改：`src/settings/include/persistentinfo.h`
- 修改：`src/settings/src/persistentinfo.cpp`
- 修改：`src/settings/CMakeLists.txt`
- 修改：`src/app/main.cpp`
- 修改：`src/app/main_ui2.cpp`
- 修改：`src/app/klogg_grep.cpp`
- 修改：`src/app/applicationrunner.cpp`
- 修改：`tests/ui2/settingsoverridetest.cpp`
- 修改：`tests/ui2/brandcontracttest.cpp`
- 修改：`tests/ui2/optionsthemetest.cpp`
- 修改：`tests/ui2/runtimecontracttest.cpp`
- 修改：`tests/ui2/documenttabclosetest.cpp`
- 修改：`tests/unit/tests_main.cpp`
- 修改：`tests/ui/qtests_main.cpp`

- [ ] **步骤 1：先把设置路径契约改成 StorageContext**

删除测试中的 `PersistentInfo::ForcePortable` 和 `setPersistentSettingsOverrideForProcess()`，改为：

```cpp
const StorageLocation location{ StorageMode::CustomDirectory, settingsRoot.path(),
                                settingsRoot.filePath( "locator.ini" ), true };
QVERIFY( StorageContext::install( location ) );
QVERIFY( StorageContext::current().ensureDirectories() );

auto& appSettings = PersistentInfo::getSettings( app_settings{} );
auto& sessionSettings = PersistentInfo::getSettings( session_settings{} );
QCOMPARE( QDir::cleanPath( appSettings.fileName() ),
          QDir::cleanPath( settingsRoot.filePath( "config/ZzLogg.ini" ) ) );
QCOMPARE( QDir::cleanPath( sessionSettings.fileName() ),
          QDir::cleanPath( settingsRoot.filePath( "session/ZzLogg_session.ini" ) ) );
```

- [ ] **步骤 2：运行测试确认仍走旧路径或无法编译**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_settings_override_test --parallel 8
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.settings_override$"
```

预期：失败，因为 PersistentInfo 尚未消费 StorageContext。

- [ ] **步骤 3：删除编译时便携状态并直接打开两个 INI**

`persistentinfo.h` 删除 `ForcePortable`、`kloggPortableConfigPath()`、override API、`PreparePortableSettings()` 和 `PrepareOsSettings()`。任务 7 已保证 GUI/grep 在读取配置前安装 Context；构造函数改为：

```cpp
PersistentInfo::PersistentInfo()
{
    const auto& storage = StorageContext::current();
    QString error;
    if ( !storage.ensureDirectories( &error ) ) {
        throw std::runtime_error( error.toStdString() );
    }
    appSettings_ = std::make_unique<QSettings>( storage.configFilePath(), QSettings::IniFormat );
    sessionSettings_
        = std::make_unique<QSettings>( storage.sessionFilePath(), QSettings::IniFormat );
    UpdateSettings();
}
```

保留 `UpdateSettings()` 的 schema 升级。删除 `main.cpp`、`main_ui2.cpp`、grep 中的静态 ForcePortable 定义；任务 7 已删除 UI2 smoke 对旧 portable path 和 QSettings override 的依赖。

所有测试进程也必须在首次 `Configuration::get*()` 前安装隔离 Context。对已有 `QTemporaryDir settingsDir` 的测试使用：

```cpp
if ( !StorageContext::install(
         { StorageMode::CustomDirectory, settingsDir.path(),
           settingsDir.filePath( "storage.ini" ), true } ) ) {
    return 1;
}
```

`runtimecontracttest.cpp` 使用现有 `ZZLOGG_TEST_SETTINGS_ROOT` 作为 dataRoot；`tests/unit/tests_main.cpp` 与 `tests/ui/qtests_main.cpp` 在 QApplication 之后创建生命周期覆盖整个测试运行的 `QTemporaryDir`。删除上述六个测试文件和单文件关闭测试中的所有 `PersistentInfo::ForcePortable` 定义。

- [ ] **步骤 4：链接存储库并运行设置测试**

`klogg_settings` PUBLIC 链接 `klogg_storage`。运行：

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_settings_override_test zzlogg_theme_configuration_test --parallel 8
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.(settings_override|theme_configuration)$"
```

预期：两个测试通过，实际生成 config/session 子目录。

- [ ] **步骤 5：提交 PersistentInfo 切换**

```powershell
git add src/settings src/app/main.cpp src/app/main_ui2.cpp src/app/klogg_grep.cpp src/app/applicationrunner.cpp tests/ui2 tests/unit/tests_main.cpp tests/ui/qtests_main.cpp
git commit -m "refactor: route settings through storage context"
```

## 任务 5：统一日志和崩溃路径

**文件：**
- 修改：`src/logging/include/logger.h`
- 修改：`src/logging/src/logger.cpp`
- 修改：`src/logging/CMakeLists.txt`
- 修改：`src/crash_handler/include/crashhandler.h`
- 修改：`src/crash_handler/src/crashhandler.cpp`
- 修改：`src/crash_handler/CMakeLists.txt`
- 创建：`tests/ui2/storageconsumerstest.cpp`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：编写日志和 crash 路径失败测试**

公开只读诊断 API：

```cpp
namespace logging {
QString currentLogFilePath();
}

QString crashDatabasePath();
```

测试先安装临时 StorageContext，启用文件日志并输出一条 `qInfo()`，然后断言：

```cpp
QVERIFY( logging::currentLogFilePath().startsWith( context.logsDirectory() ) );
QVERIFY( QFileInfo::exists( logging::currentLogFilePath() ) );
QCOMPARE( QDir::cleanPath( crashDatabasePath() ),
          QDir::cleanPath( context.crashesDirectory() ) );
```

- [ ] **步骤 2：运行测试确认旧日志仍在 temp**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_storage_consumers_test --parallel 8
```

预期：构建失败或日志路径断言失败。

- [ ] **步骤 3：修改 Logger**

`enableFileLogging()` 在打开文件前 `QDir{}.mkpath(StorageContext::current().logsDirectory())`，文件名改为 `ZzLogg_<yyyy-MM-dd_HH-mm-ss>_<pid>.log`。打开失败时保留 Qt console handler 并通过 `std::cerr` 输出目标路径和 `QFile::errorString()`，避免日志系统递归记录自身错误。`currentLogFilePath()` 在锁内返回空字符串或绝对文件名。

- [ ] **步骤 4：修改 CrashHandler**

将匿名 `sentryDatabasePath()` 提升为始终编译的 `crashDatabasePath()`，直接返回 `StorageContext::current().crashesDirectory()`。删除 `KLOGG_PORTABLE` 和 `QStandardPaths` 分支；Sentry 构造函数继续创建目录并使用该路径。`klogg_crash_handler` PUBLIC 链接 `klogg_storage`。

- [ ] **步骤 5：运行消费者测试**

```powershell
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.storage_consumers$"
```

预期：日志文件和 crash 路径均位于测试根目录，测试退出时关闭文件日志。

- [ ] **步骤 6：提交消费者切换**

```powershell
git add src/logging src/crash_handler tests/ui2/storageconsumerstest.cpp tests/ui2/CMakeLists.txt
git commit -m "feat: store logs and crashes in data root"
```

## 任务 6：构建可复用的存储位置选择 UI

**文件：**
- 创建：`src/ui/include/storagelocationpage.h`
- 创建：`src/ui/src/storagelocationpage.cpp`
- 创建：`src/ui/include/storagebootstrapdialog.h`
- 创建：`src/ui/src/storagebootstrapdialog.cpp`
- 创建：`tests/ui2/storagelocationuitest.cpp`
- 修改：`src/ui/CMakeLists.txt`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：编写页面状态和首次向导失败测试**

固定页面 API：

```cpp
class StorageLocationPage final : public QWidget {
    Q_OBJECT
  public:
    explicit StorageLocationPage( QWidget* parent = nullptr );
    void setApplicationDirectory( QString path );
    void setUserDataDirectory( QString path );
    void setLocation( const StorageLocation& location );
    StorageLocation location() const;
    bool isSelectionValid() const;
    QString validationError() const;
    void setCommandLineManaged( bool managed );
  Q_SIGNALS:
    void validityChanged( bool valid );
};

class StorageBootstrapDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit StorageBootstrapDialog( QWidget* parent = nullptr );
    void configurePaths( const QString& applicationDirectory,
                         const QString& userDataDirectory );
    std::optional<StorageLocation> selectedLocation() const;
};
```

测试用 `findChild()` 的固定 objectName `userStorageRadio`、`programStorageRadio`、`customStorageRadio`、`customStoragePath`、`storageValidationLabel`、`storageContinueButton` 驱动页面，断言程序模式预览 `<app>/data`、无效自定义目录禁用继续按钮、命令行托管状态禁用所有编辑控件。

- [ ] **步骤 2：运行测试确认 UI 类型不存在**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_storage_location_ui_test --parallel 8
```

预期：构建失败，报告页面/对话框头文件不存在。

- [ ] **步骤 3：实现页面和首次向导**

页面使用 `QRadioButton`、只读路径预览、`QLineEdit`、浏览按钮、打开目录按钮和错误标签；所有 objectName 与测试一致。每次模式或路径变化调用 `StorageValidator::validate()`，程序模式同时验证相邻 locator 可写。首次对话框标题为“选择 ZzLogg 数据保存位置”，取消直接返回 Rejected，不创建文件。

- [ ] **步骤 4：运行 UI 测试**

```powershell
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.storage_location_ui$"
```

预期：页面模式、路径预览、验证和按钮状态全部通过。

- [ ] **步骤 5：提交存储选择 UI**

```powershell
git add src/ui/include/storagelocationpage.h src/ui/src/storagelocationpage.cpp src/ui/include/storagebootstrapdialog.h src/ui/src/storagebootstrapdialog.cpp src/ui/CMakeLists.txt tests/ui2/storagelocationuitest.cpp tests/ui2/CMakeLists.txt
git commit -m "feat: add storage location selector"
```

## 任务 7：在配置初始化前完成启动解析

**文件：**
- 创建：`src/app/storagebootstrap.h`
- 创建：`src/app/storagebootstrap.cpp`
- 创建：`tests/ui2/storagebootstraptest.cpp`
- 修改：`src/app/cli.h`
- 修改：`src/app/applicationrunner.cpp`
- 修改：`src/app/klogg_grep.cpp`
- 修改：`src/app/CMakeLists.txt`
- 修改：`tests/ui2/CMakeLists.txt`
- 修改：`tests/ui2/applicationsmoke.cmake`
- 修改：`tests/ui2/sessionrestoresmoke.cmake`
- 修改：`tests/ui2/asyncobjectlifetimesmoke.cmake`
- 修改：`tests/ui2/titlebarlifetimesmoke.cmake`
- 修改：`tests/ui2/smokesetupdiagnosticstest.cmake`
- 删除：`tests/ui2/portableguardsmoke.cmake`

- [ ] **步骤 1：编写 bootstrap 决策失败测试**

固定协调接口：

```cpp
struct StorageBootstrapPrompt {
    QString applicationDirectory;
    QString userDataDirectory;
};
using StorageSelectionProvider
    = std::function<std::optional<StorageLocation>( const StorageBootstrapPrompt& )>;

enum class StorageBootstrapStatus { Ready, Cancelled, Error };
struct StorageBootstrapResult { StorageBootstrapStatus status; QString error; };

StorageBootstrapResult bootstrapStorage( const QString& applicationDirectory,
                                         const QString& appConfigDirectory,
                                         const QString& userDataDirectory,
                                         const QString& oldCrashDirectory,
                                         const QString& commandLineDataRoot,
                                         StorageSelectionProvider selectionProvider );
```

测试覆盖：CLI 路径不调用 provider；已有 locator 不调用 provider；无 locator/无旧数据调用一次 provider；provider 取消返回 Cancelled 且不安装 Context；旧 portable 数据自动迁移到 `<app>/data`；pending 事务在读取 Configuration 前恢复。

- [ ] **步骤 2：运行 bootstrap 测试确认失败**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_storage_bootstrap_test --parallel 8
```

预期：构建失败，报告 `storagebootstrap.h` 不存在。

- [ ] **步骤 3：给 CLI 增加一次性覆盖参数**

`CliParameters` 增加 `QString data_dir`，GUI 和 console 都注册：

```cpp
const QCommandLineOption dataDirOption(
    QStringLiteral( "data-dir" ),
    QStringLiteral( "use an absolute data directory for this process" ),
    QStringLiteral( "path" ) );
parser.addOption( dataDirOption );
// process 后：data_dir = parser.value( dataDirOption );
```

非绝对路径由 bootstrap 返回 Error。该参数只覆盖本进程，不写 locator。

- [ ] **步骤 4：实现 bootstrap 协调器**

协调器严格按照 CLI、程序 locator、用户 locator、旧数据、provider 的顺序解析；成功后调用 `StorageContext::install()`。首次 provider 选择成功后先写 manifest 和 active locator，再安装 Context。旧数据通过 StorageMigrator 迁移。任何错误返回具体路径，不默认创建另一套空数据。

- [ ] **步骤 5：重排 GUI 启动顺序**

`runKloggApplication()` 改为：

```cpp
prepareZzLoggApplicationIdentity();
setApplicationAttributes( true, 0 );
KloggApp app{ argc, argv };
CliParameters parameters{ app };

if ( !parameters.multi_instance && app.isSecondary() ) {
    app.sendFilesToPrimaryInstance( parameters.filenames );
    return app.exec();
}

const auto storageResult = bootstrapStorage(
    QCoreApplication::applicationDirPath(),
    QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ),
    QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ),
    QStandardPaths::writableLocation( QStandardPaths::AppDataLocation ) + "/klogg_dump",
    parameters.data_dir,
    []( const StorageBootstrapPrompt& prompt ) -> std::optional<StorageLocation> {
        StorageBootstrapDialog dialog;
        dialog.configurePaths( prompt.applicationDirectory, prompt.userDataDirectory );
        return dialog.exec() == QDialog::Accepted ? dialog.selectedLocation() : std::nullopt;
    } );
if ( storageResult.status != StorageBootstrapStatus::Ready ) {
    return storageResult.status == StorageBootstrapStatus::Cancelled
               ? EXIT_SUCCESS : EXIT_FAILURE;
}
const auto& config = Configuration::getSynced();
```

UI2 smoke 环境不再安装 QSettings override；各 CMake smoke 命令显式追加 `--data-dir <isolated-root>`。删除 portable guard smoke，因为相邻 locator 已由 locator 单元测试覆盖。

- [ ] **步骤 6：修改 grep 的无向导行为**

grep 先解析 `CliParameters(app, true)`，用空 provider 调用 bootstrap；无 locator 时直接构造 UserDirectory 的默认 StorageLocation，不写“首次 GUI 已完成”locator。增加参数校验：没有输入文件或 pattern 时输出帮助并返回失败，避免访问空 `filenames.front()`。

- [ ] **步骤 7：运行 bootstrap 与烟雾测试**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_storage_bootstrap_test zzlogg_ui2 --parallel 8
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.(storage_bootstrap|application_smoke|session_restore_smoke|async_object_lifetime_smoke|titlebar_lifetime_smoke|smoke_setup_diagnostics)$"
```

预期：bootstrap 单元测试和所有更新后的隔离 smoke 通过，执行期间不读写真实用户设置。

- [ ] **步骤 8：提交启动集成**

```powershell
git add src/app tests/ui2
git commit -m "feat: select storage before application settings"
```

## 任务 8：在 Preferences 中调度迁移并支持安全重启

**文件：**
- 修改：`src/ui/include/optionsdialog.h`
- 修改：`src/ui/src/optionsdialog.cpp`
- 修改：`src/ui/include/mainwindow.h`
- 修改：`src/ui/src/mainwindow.cpp`
- 修改：`src/app/kloggapp.h`
- 修改：`src/app/applicationrunner.h`
- 修改：`src/app/main_ui2.cpp`
- 创建：`tests/ui2/storagesettingstest.cpp`
- 创建：`tests/ui2/restartcontracttest.cpp`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：编写设置调度与重启契约失败测试**

`OptionsDialog` 新增信号：

```cpp
Q_SIGNALS:
    void optionsChanged();
    void restartRequested();
```

固定进程退出码：

```cpp
inline constexpr int ZzLoggRestartExitCode = 773;
```

设置测试安装临时 StorageContext，构造 OptionsDialog，找到 objectName `storageLocationPage`，选择另一个空目录并点击 Apply。断言当前 StorageContext 未改变、当前 locator 出现 pending、对话框发出一次 `restartRequested`（测试通过设置属性 `zzlogg.test.restartAnswer=now` 避免模态消息框）。命令行托管 context 断言页面禁用且不会写 pending。

重启契约测试构造 KloggApp/MainWindow，触发 `restartRequested`，断言窗口 closeEvent 执行、session 同步，事件循环返回 773。

- [ ] **步骤 2：运行新测试确认失败**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target zzlogg_storage_settings_test zzlogg_restart_contract_test --parallel 8
```

预期：构建失败，报告存储页或 restart 信号不存在。

- [ ] **步骤 3：把 StorageLocationPage 加入 Preferences**

`OptionsDialog` 构造函数用 `tabWidget->addTab(storageLocationPage_, tr("Storage"))` 添加独立页面并设置 `storageLocationPage` objectName。初始化显示 `StorageContext::current().location()`；命令行覆盖时调用 `setCommandLineManaged(true)`。

Apply 时先执行现有 `config.save()`、SavedSearches 和 RecentFiles 保存，再比较新旧位置。不同且有效时创建带 UUID 的 StorageMigrationRequest，调用当前 StorageLocatorStore 的 `writePending()`。写失败显示错误且不关闭 Preferences。

- [ ] **步骤 4：实现立即重启/稍后重启**

pending 写成功后显示 `QMessageBox`，按钮文本“立即重启”和“稍后重启”。选择立即重启发出 `restartRequested()`；稍后只关闭提示。测试属性存在时不显示消息框，按属性值 `now` 或 `later` 选择。

`MainWindow::options()` 把对话框信号转发为新的 `MainWindow::restartRequested()`。`KloggApp::newWindow()` 连接该信号到 `restartApplication()`；该函数按现有退出顺序关闭所有窗口，最后 `QCoreApplication::exit(ZzLoggRestartExitCode)`。

`main_ui2.cpp` 保存原始可执行路径和 `argv[1..]`，在 `runKloggApplication()` 返回 773、KloggApp 已析构后调用：

```cpp
QProcess::startDetached( executablePath, arguments );
return EXIT_SUCCESS;
```

普通退出码原样返回。若启动参数包含 `--data-dir`，原样保留，设置页已禁用切换，因此不会与 pending 冲突。

- [ ] **步骤 5：运行设置、重启和现有 Options 测试**

```powershell
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.(storage_settings|restart_contract|options_theme)$"
```

预期：三组测试通过。

- [ ] **步骤 6：提交设置切换**

```powershell
git add src/ui/include/optionsdialog.h src/ui/src/optionsdialog.cpp src/ui/include/mainwindow.h src/ui/src/mainwindow.cpp src/app/kloggapp.h src/app/applicationrunner.h src/app/main_ui2.cpp tests/ui2/storagesettingstest.cpp tests/ui2/restartcontracttest.cpp tests/ui2/CMakeLists.txt
git commit -m "feat: change storage location from preferences"
```

## 任务 9：合并成唯一正式 GUI 和统一运行文件夹

**文件：**
- 修改：`CMakeLists.txt`
- 修改：`cmake/ZzPureTools.cmake`
- 修改：`cmake/prepare_version.cmake`
- 修改：`src/CMakeLists.txt`
- 修改：`src/app/CMakeLists.txt`
- 修改：`src/app/main.cpp`
- 删除：`src/app/main_ui2.cpp`
- 修改：`CMakePresets.json`
- 修改：`CMakeUserPresets.json.example`
- 修改：`.github/actions/agent-package-win/action.yml`
- 修改：`tests/ui2/CMakeLists.txt`
- 修改：`tests/ui2/outputcontracttest.cmake`
- 修改：`tests/ui2/versionresourcecontracttest.cmake`
- 修改：`tests/ui2/versionresourcecontracttest.ps1`
- 修改：`tests/ui2/windowsinstallercontracttest.cmake`

- [ ] **步骤 1：先修改输出契约使旧目标存在时失败**

`outputcontracttest.cmake` 只接受：

```cmake
get_filename_component(main_name "${MAIN_APP}" NAME)
if(NOT main_name STREQUAL "ZzLogg${EXECUTABLE_SUFFIX}")
  message(FATAL_ERROR "Expected only ZzLogg${EXECUTABLE_SUFFIX}, got ${main_name}")
endif()
if(TARGET_PORTABLE_EXISTS OR TARGET_UI2_EXISTS)
  message(FATAL_ERROR "Separate portable/UI2 GUI targets must not exist")
endif()
if(NOT GREP_EXCLUDED_FROM_ALL)
  message(FATAL_ERROR "klogg_grep must remain EXCLUDE_FROM_ALL")
endif()
```

CMake 测试调用通过 `$<TARGET_EXISTS:klogg_portable>`、`$<TARGET_EXISTS:zzlogg_ui2>` 传布尔值，并检查统一运行目录名为 `ZzLogg-runtime`、其中存在 `ZzLogg.exe`。

- [ ] **步骤 2：运行输出契约确认失败**

```powershell
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.output_contract$"
```

预期：失败并指出旧 portable/UI2 目标仍存在。

- [ ] **步骤 3：让 UI2 成为 klogg 的唯一入口**

把 `main_ui2.cpp` 的 ZzWindowKit bootstrap、runtime factory 和重启逻辑移入 `main.cpp`，删除旧 legacy main 内容和 `main_ui2.cpp`。根 CMake 无条件 `include(ZzPureTools)` / `klogg_add_zzpuretools()`，`src/CMakeLists.txt` 无条件添加 `ui2`。

`src/app/CMakeLists.txt`：

- `klogg` 使用 `main.cpp`、链接 `zzlogg_ui2_runtime`、启用 C++20 并调用 `klogg_copy_ui2_runtime_dlls(klogg)`。
- 删除 `klogg_portable` 和 `zzlogg_ui2` 的 add_executable、属性、链接、LTO、资源和符号处理。
- `klogg_grep` 保持 `EXCLUDE_FROM_ALL`。
- Windows 运行文件夹目标命名 `zzlogg_runtime_folder`，输出 `${CMAKE_BINARY_DIR}/runtime/$<CONFIG>/ZzLogg-runtime`，部署输入为 `$<TARGET_FILE:klogg>`。
- `ci_build` 只依赖 `klogg`。

- [ ] **步骤 4：删除多 GUI 版本资源并整理 presets**

`prepare_version.cmake` 只生成 Product 和 Grep 资源。根选项改为 `KLOGG_BUILD_UI_TESTS`；读取旧 `KLOGG_BUILD_UI2_TESTS` 时映射到新选项并输出 deprecated warning，一版后可移除。`ninja-ui2-debug`、`windows-vs2026-ui2` 等旧 preset 名保留为隐藏/兼容别名，实际继承新的正式 UI preset，不再控制另一可执行目标。示例用户 preset 改为 `windows-qt6` 一套。

- [ ] **步骤 5：更新 Windows staging 与安装契约**

GitHub action 构建 `zzlogg_runtime_folder`，直接把 `runtime/.../ZzLogg-runtime` 复制到 `release`；删除复制 `ZzLogg_portable.exe`、删除 portable 文件、再覆盖 `ZzLogg.exe` 的三步。免安装包仍可命名 `...-portable` 作为分发形式，但内容只有同一个 `ZzLogg.exe`。

安装契约断言 runtime 文件夹包含 Qt plugins、ZzPureTools DLL、TBB、`ZzLogg.exe`，不含 `_portable.exe` 或 `_ui2.exe`；NSIS manifest 继续只删除 installer-owned 文件。

- [ ] **步骤 6：重新配置、构建并运行三个发布契约**

旧 build graph 已删除目标，必须重新配置：

```powershell
cmake --preset windows-vs2026-ui2 -DCMAKE_GENERATOR_INSTANCE="D:/SoftWare/Microsoft Visual Studio/18/Community" -DCMAKE_PREFIX_PATH="D:/SoftWare/Qt/6.11.0/msvc2022_64" -DKLOGG_USE_HYPERSCAN=OFF
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --parallel 8
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.(output_contract|version_resource_contract|windows_installer_contract)$"
```

预期：三个契约通过，输出目录不存在 `ZzLogg_portable.exe` 和 `ZzLogg_ui2.exe`，grep 不在默认构建输出中。

- [ ] **步骤 7：提交目标合并**

```powershell
git add CMakeLists.txt CMakePresets.json CMakeUserPresets.json.example cmake src .github/actions/agent-package-win/action.yml tests/ui2
git commit -m "build: publish one ZzLogg GUI executable"
```

## 任务 10：翻译、文档和端到端验收

**文件：**
- 修改：`src/app/i18n/en.ts`
- 修改：`src/app/i18n/zh_CN.ts`
- 修改：`src/app/i18n/zh_TW.ts`
- 修改：`docs/BUILD.md`
- 修改：`README.md`
- 创建：`tests/ui2/storageapplicationsmoke.cmake`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：添加三模式端到端 smoke**

`storageapplicationsmoke.cmake` 对同一个 `$<TARGET_FILE:klogg>` 运行三个隔离场景：

1. `--data-dir <custom>` 启动 smoke，断言 config/session/logs 在 custom 下。
2. 预写程序旁 locator 指向相对 `data`，复制 runtime 后启动，断言该 runtime/data 下生成全部持久目录，隔离 APPDATA 中没有业务 INI。
3. 预写用户 locator 指向 user root，启动并恢复 session，断言用户 root 中配置和会话可读。

每个进程设置 `ZZLOGG_UI2_SMOKE_MS` 自动退出，使用独立 APPDATA/LOCALAPPDATA/XDG_CONFIG_HOME。测试完成后检查目标根：

```cmake
foreach(relative_path IN ITEMS
    config/ZzLogg.ini
    session/ZzLogg_session.ini
    storage-manifest.ini)
  if(NOT EXISTS "${data_root}/${relative_path}")
    message(FATAL_ERROR "Missing persistent data: ${relative_path}")
  endif()
endforeach()
```

启用 `--log` 场景并断言 `logs/*.log` 至少一个。CrashHandler 路径由任务 5 单元测试覆盖，因为默认构建关闭 Sentry。

- [ ] **步骤 2：运行新 smoke 确认遗漏**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --target klogg --parallel 8
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure -R "^zzlogg_ui2\.storage_application_smoke$"
```

预期：如果任一启动路径没有写入统一目录，测试给出缺失的相对文件；修正启动/locator 接线直到通过。

- [ ] **步骤 3：更新翻译**

运行 `cmake --build out/ui2-vs --config RelWithDebInfo --target lupdate`，然后为所有新增 StorageLocationPage、BootstrapDialog、迁移错误和重启提示补齐：

- `en.ts`：英文 source 对应英文 translation。
- `zh_CN.ts`：简体中文“存储”“用户目录”“程序目录”“自定义目录”“立即重启”“稍后重启”等。
- `zh_TW.ts`：对应繁体中文。

运行 `lrelease` 所在的正常 app 构建，确认没有 TS XML 错误。

- [ ] **步骤 4：更新构建和用户文档**

`docs/BUILD.md` 删除“可选 UI2”“portable executable”描述，写明 Qt 6.8+ 与 ZzPureTools 子模块为正式构建依赖；列出唯一 GUI `ZzLogg.exe`、非默认 grep、统一 runtime folder target 和 presets。`README.md` 增加首次启动存储选择说明，并解释程序目录模式等价于绿色使用。

- [ ] **步骤 5：运行全量验证**

```powershell
cmake --build --preset windows-vs2026-ui2-relwithdebinfo --parallel 8
ctest --test-dir out/ui2-vs -C RelWithDebInfo --output-on-failure
git diff --check
git status --short
```

预期：全量构建成功、所有 CTest 通过、diff 无空白错误；状态只包含本任务计划内文件，不包含主工作区原有 `serach.png` / `serach.svg`。

- [ ] **步骤 6：执行人工启动验收**

在临时 runtime 副本中依次验证：

- 删除所有 locator 后启动，首次向导出现；取消后不创建配置。
- 选择程序目录后生成 `data/`，打开文件、修改主题、启用日志、退出再启动，状态恢复。
- 从 Preferences 切到自定义目录，选择立即重启，迁移后状态和最近文件保留。
- 将自定义目录临时改名，启动显示恢复错误而不是空白默认配置。
- 单文件标签仍可关闭到零文档。

记录使用的 runtime 路径和每项结果到最终交付消息，不把临时目录加入 Git。

- [ ] **步骤 7：提交文档与最终 smoke**

```powershell
git add src/app/i18n docs/BUILD.md README.md tests/ui2/storageapplicationsmoke.cmake tests/ui2/CMakeLists.txt
git commit -m "docs: document unified ZzLogg storage"
```
