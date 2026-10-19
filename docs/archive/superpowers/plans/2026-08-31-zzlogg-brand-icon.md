# ZzLogg 品牌名称与应用图标实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将应用的用户可见身份、配置路径、构建产物、平台包和图标统一为 ZzLogg，同时保留内部 `klogg_*` 目标与源码符号。

**架构：** CMake 是品牌数据的唯一配置源，通过生成的只读 C++ 头把产品名、供应方、主页、配置名和资源路径提供给运行时。现有矢量图标继续作为母版，通过仅在显式启用时构建的 Qt 图标工具生成 PNG、ICO 与 ICNS；普通应用构建只读取已提交资产。所有运行时入口继续经过 `runKloggApplication()`，所以应用身份与窗口图标只设置一次；更新检查在没有 ZzLogg 发布清单时由空清单 URL 明确关闭。

**技术栈：** CMake 3.25+、C++17/C++20、Qt 6 Core/Gui/Widgets/Svg/Test、CTest、NSIS、CPack、SVG/PNG/ICO/ICNS。

**依据规格：** `docs/superpowers/specs/2026-08-31-zzlogg-brand-icon-design.md`

---

## 基线与环境

- 隔离工作区：`D:\File\Program\GitCode\ZzLogg-branding`
- 分支：`codex/zzlogg-branding`
- Qt：`D:\SoftWare\Qt\6.11.0\msvc2022_64`
- Visual Studio：`D:\SoftWare\Microsoft Visual Studio\18\Community`
- CMake：`D:\SoftWare\CMake\bin\cmake.exe`
- 2026-08-31 的基线命令：

```powershell
$env:CMAKE_PREFIX_PATH = 'D:\SoftWare\Qt\6.11.0\msvc2022_64'
& 'D:\SoftWare\CMake\bin\cmake.exe' --preset windows-vs2026-ui2 -DKLOGG_USE_HYPERSCAN=OFF
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug
```

- 基线结果：构建成功，UI2 测试 11/11 通过。
- 本机没有 Boost 头文件；干净配置必须显式传入 `-DKLOGG_USE_HYPERSCAN=OFF`。这只是本地验证前提，不改变项目默认功能选项。
- 根工作区的 `serach.png`、`serach.svg` 是用户文件，不读取为构建输入、不修改、不删除、不提交。

## 文件结构

### 品牌契约与配置身份

- 创建 `cmake/ZzLoggBrand.cmake`：集中定义产品元数据，生成 C++ 头并声明 `zzlogg_brand` 接口目标。
- 创建 `cmake/zzlogg_brand.h.in`：把 CMake 品牌值映射成只读 C++ 字符串常量。
- 修改 `CMakeLists.txt`：加载品牌契约，并让 Windows 版本资源与 CPack 使用 ZzLogg 元数据。
- 修改 `src/settings/CMakeLists.txt`：让设置库依赖品牌契约。
- 修改 `src/settings/src/persistentinfo.cpp`：切换普通、会话和便携配置身份。
- 修改 `tests/ui2/settingsoverridetest.cpp`：验证 `ZzLogg/ZzLogg.ini` 与 `ZzLogg/ZzLogg_session.ini`。
- 修改 `tests/ui2/isolatedsettingscheck.cmake`、`tests/ui2/portableguardsmoke.cmake`：验证新配置文件名并继续隔离真实用户目录。
- 创建 `tests/ui2/brandcontracttest.cpp`：验证集中常量、配置名、主页、标识符和默认关闭的更新清单。

### 图标母版与派生资源

- 移动并修改 `src/app/images/hicolor/scalable/klogg.svg` → `src/app/images/hicolor/scalable/ZzLogg.svg`：唯一矢量母版，加入固定轮廓 `z`、多色日志块和琥珀橙把手。
- 移动并重新生成 `src/app/images/hicolor/{16x16,32x32,48x48}/klogg.png` → 对应目录的 `ZzLogg.png`。
- 创建 `src/app/images/hicolor/{64x64,128x128,256x256,512x512}/ZzLogg.png`。
- 移动并重新生成 `Resources/klogg.ico` → `Resources/ZzLogg.ico`。
- 移动并重新生成 `Resources/klogg.icns` → `Resources/ZzLogg.icns`。
- 创建 `tools/CMakeLists.txt`、`tools/icon_generator/CMakeLists.txt`、`tools/icon_generator/main.cpp`：显式启用的确定性图标生成器。
- 创建 `tests/ui2/iconassettest.cpp`：检查 SVG 结构、PNG 尺寸/透明通道、ICO 帧与 ICNS 表示。
- 修改 `src/app/klogg.qrc`：用稳定别名暴露 ZzLogg SVG 和 PNG。

### 运行时品牌与外部链接

- 创建 `src/app/zzloggapplicationidentity.h`、`src/app/zzloggapplicationidentity.cpp`：集中设置 QApplication 名称、显示名、组织信息与窗口图标。
- 修改 `src/app/CMakeLists.txt`、`src/app/applicationrunner.cpp`：三个 GUI 目标共用身份初始化。
- 修改 `src/app/cli.h`、`src/app/kloggapp.h`：更新命令行与新版本提示中的产品文案。
- 修改 `src/ui/src/mainwindow.cpp`、`src/ui/src/mainwindowtext.cpp`：窗口、托盘、消息框、帮助和关于页使用 ZzLogg。
- 修改 `src/versioncheck/include/versionchecker.h`、`src/versioncheck/src/versionchecker.cpp`：空清单 URL 时不建立网络请求。
- 修改 `src/crash_handler/include/issuereporter.h`、`src/crash_handler/src/issuereporter.cpp`：问题报告改到 GitCode，并提供可测试的纯 URL 构造函数。
- 删除 `latest.json`：移除仍指向上游 klogg 发布的无效清单。
- 修改 `src/app/i18n/en.ts`、`src/app/i18n/zh_CN.ts`、`src/app/i18n/zh_TW.ts`：同步用户可见品牌文案。

### 产物、便携目录与平台包

- 修改 `src/app/CMakeLists.txt`：通过 `OUTPUT_NAME` 生成 `ZzLogg`、`ZzLogg_portable`、`ZzLogg_ui2`、`ZzLogg_grep`，并把便携目录改为 `ZzLogg-portable`。
- 修改 `cmake/prepare_version.cmake`：Windows 版本资源使用 ZzLogg 产品元数据。
- 创建 `tests/ui2/outputcontracttest.cmake`：验证默认产物名、便携目录和 grep 排除规则。
- 修改 `tests/ui2/CMakeLists.txt`：注册品牌、图标与产物契约测试。
- 移动并修改 `packaging/linux/klogg.desktop` → `packaging/linux/ZzLogg.desktop`。
- 移动并修改 `packaging/windows/klogg.nsi` → `packaging/windows/ZzLogg.nsi`。
- 修改 `packaging/osx/distribution.xml`、`packaging/osx/dmg_setup.scpt`、`packaging/description.txt`。
- 修改 `CMakeLists.txt` 中 Linux 图标安装、desktop 安装、macOS Bundle、DEB/RPM 与 CPack 元数据。
- 删除已失效的 `AppImageBuilder.yml`、`packaging/linux/appimage/generate_appimage.sh`、`packaging/linux/arch/PKGBUILD`、`packaging/linux/gentoo/klogg-22.06.0.1289.ebuild`、`packaging/linux/deb`、`packaging/linux/rpm`。
- 删除仍下载上游 Qt 5 包的 `packaging/windows/chocolatey/`、`packaging/windows/scoop/`。
- 删除已被 CMake 绿色目录替代且会生成 zip 的 `packaging/windows/prepare_release.cmd`、`packaging/windows/7z_klogg_listfile.txt`、`packaging/windows/7z_pdb_listfile.txt`。
- 创建 `cmake/VerifyExternalBrand.cmake`：检查当前发行入口没有上游地址、旧应用标识或旧外部文件名。
- 修改 `README.md`、`docs/BUILD.md`、`docs/DOCUMENTATION.md`：只声明当前确实存在的 ZzLogg 构建与发行方式，并保留上游来源和版权归属章节。

---

### 任务 1：建立集中品牌契约和全新配置身份

**文件：**
- 创建：`cmake/ZzLoggBrand.cmake`
- 创建：`cmake/zzlogg_brand.h.in`
- 修改：`CMakeLists.txt:10-23`
- 修改：`src/settings/CMakeLists.txt:15-23`
- 修改：`src/settings/src/persistentinfo.cpp:56-72,76-89,138-169`
- 修改：`tests/ui2/CMakeLists.txt`
- 创建：`tests/ui2/brandcontracttest.cpp`
- 修改：`tests/ui2/settingsoverridetest.cpp:18-55`
- 修改：`tests/ui2/isolatedsettingscheck.cmake:1-12`
- 修改：`tests/ui2/portableguardsmoke.cmake:1-44`

- [ ] **步骤 1：先写品牌与配置路径失败测试**

在 `tests/ui2/brandcontracttest.cpp` 中直接断言规格中的常量；在现有设置测试中把期望路径改成新身份：

```cpp
#include "zzlogg_brand.h"
#include <QtTest>

class BrandContractTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void exposesApprovedIdentity()
    {
        QCOMPARE( QString::fromLatin1( zzlogg::brand::ProductName ),
                  QStringLiteral( "ZzLogg" ) );
        QCOMPARE( QString::fromLatin1( zzlogg::brand::ProductDescription ),
                  QStringLiteral( "ZzLogg log viewer" ) );
        QCOMPARE( QString::fromLatin1( zzlogg::brand::Vendor ),
                  QStringLiteral( "JackfahdinQt" ) );
        QCOMPARE( QString::fromLatin1( zzlogg::brand::HomepageUrl ),
                  QStringLiteral( "https://gitcode.com/JackfahdinQt/ZzLogg" ) );
        QCOMPARE( QString::fromLatin1( zzlogg::brand::ApplicationIdentifier ),
                  QStringLiteral( "com.gitcode.jackfahdinqt.zzlogg" ) );
        QVERIFY( QString::fromLatin1( zzlogg::brand::UpdateManifestUrl ).isEmpty() );
    }
};
```

`settingsoverridetest.cpp` 的路径必须精确为：

```cpp
const QString expectedAppPath
    = QDir( settingsRoot.path() ).filePath( QStringLiteral( "ZzLogg/ZzLogg.ini" ) );
const QString expectedSessionPath
    = QDir( settingsRoot.path() ).filePath( QStringLiteral( "ZzLogg/ZzLogg_session.ini" ) );
```

- [ ] **步骤 2：运行测试并确认红灯来自缺失契约或旧路径**

运行：

```powershell
$env:CMAKE_PREFIX_PATH = 'D:\SoftWare\Qt\6.11.0\msvc2022_64'
& 'D:\SoftWare\CMake\bin\cmake.exe' --preset windows-vs2026-ui2 -DKLOGG_USE_HYPERSCAN=OFF
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug --target zzlogg_brand_contract_test zzlogg_settings_override_test
```

预期：配置或编译失败，明确报告 `zzlogg_brand.h` 不存在；若先加入测试目标后再编译，则设置测试仍因实际路径为 `klogg/klogg.ini` 而失败。

`tests/ui2/CMakeLists.txt` 把 `zzlogg_brand_contract_test` 链接到
`klogg_settings Qt6::Test Qt6::Core`，设置 `AUTOMOC ON`，CTest 名称固定为
`zzlogg_ui2.brand_contract`。

- [ ] **步骤 3：实现 CMake 单一品牌源和生成头**

`cmake/ZzLoggBrand.cmake` 先定义以下不可变值，再声明
`zzlogg_configure_brand()` 函数：

```cmake
set(ZZLOGG_PRODUCT_NAME "ZzLogg")
set(ZZLOGG_PRODUCT_DESCRIPTION "ZzLogg log viewer")
set(ZZLOGG_VENDOR "JackfahdinQt")
set(ZZLOGG_HOMEPAGE_URL "https://gitcode.com/JackfahdinQt/ZzLogg")
set(ZZLOGG_IDENTIFIER "com.gitcode.jackfahdinqt.zzlogg")
set(ZZLOGG_SETTINGS_ORGANIZATION "ZzLogg")
set(ZZLOGG_SETTINGS_APPLICATION "ZzLogg")
set(ZZLOGG_SESSION_SETTINGS_APPLICATION "ZzLogg_session")
set(ZZLOGG_PORTABLE_CONFIG_BASENAME "ZzLogg")
set(ZZLOGG_ICON_RESOURCE ":/zzlogg/icons/ZzLogg.svg")
set(ZZLOGG_UPDATE_MANIFEST_URL "")

function(zzlogg_configure_brand)
  configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/zzlogg_brand.h.in"
    "${CMAKE_BINARY_DIR}/generated/zzlogg_brand.h"
    @ONLY)
  add_library(zzlogg_brand INTERFACE)
  target_include_directories(zzlogg_brand INTERFACE "${CMAKE_BINARY_DIR}/generated")
endfunction()
```

生成头使用 `inline constexpr char[]`，命名空间固定为 `zzlogg::brand`。顶层先
`include(cmake/ZzLoggBrand.cmake)`，再让 `project(klogg ...)` 的 `DESCRIPTION`
读取 `ZZLOGG_PRODUCT_DESCRIPTION`，随后调用 `zzlogg_configure_brand()`。内部目标名
保持不变；`PROJECT_HOMEPAGE_URL`、`COMPANY`、`IDENTIFIER` 从上述变量取得。原有
`COPYRIGHT` 内容原样保留。

- [ ] **步骤 4：把 PersistentInfo 切换到 ZzLogg 身份**

`persistentinfo.cpp` 包含生成头，并用常量构造配置：

```cpp
constexpr const char PortableExtension[] = ".conf";

QString kloggPortableConfigPath()
{
    // 保留现有 whereami 路径查找代码。
    return executablePath + QDir::separator()
           + QString::fromLatin1( zzlogg::brand::PortableConfigBaseName )
           + PortableExtension;
}

appSettings_ = std::make_unique<QSettings>(
    format, QSettings::UserScope,
    QString::fromLatin1( zzlogg::brand::SettingsOrganization ),
    QString::fromLatin1( zzlogg::brand::SettingsApplication ) );
sessionSettings_ = std::make_unique<QSettings>(
    format, QSettings::UserScope,
    QString::fromLatin1( zzlogg::brand::SettingsOrganization ),
    QString::fromLatin1( zzlogg::brand::SessionSettingsApplication ) );
```

`makeSessionSettingsPath()` 使用 `SessionSettingsApplication + ".conf"`，因此便携会话文件稳定为 `ZzLogg_session.conf`。不加入旧 `klogg` 路径探测或复制逻辑。
本任务不迁移旧配置，也不读取、覆盖或删除旧 `klogg` 配置。

- [ ] **步骤 5：更新 CMake 冒烟测试里的隔离路径**

`isolatedsettingscheck.cmake` 检查 `ZzLogg/ZzLogg.ini` 和 `ZzLogg/ZzLogg_session.ini`；`portableguardsmoke.cmake` 创建并断言 `runtime/ZzLogg.conf`。所有脚本继续把 APPDATA/XDG_CONFIG_HOME 指向测试目录。

- [ ] **步骤 6：运行聚焦测试验证绿灯**

运行：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --preset windows-vs2026-ui2 -DKLOGG_USE_HYPERSCAN=OFF
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug --target zzlogg_brand_contract_test zzlogg_settings_override_test
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug -R 'zzlogg_ui2\.(brand_contract|settings_override|portable_guard_smoke)' --output-on-failure
```

预期：3 项测试全部通过；测试输出中的配置路径只出现 `ZzLogg`，不访问真实 `%APPDATA%`。

- [ ] **步骤 7：提交品牌契约和配置身份**

```powershell
git add CMakeLists.txt cmake/ZzLoggBrand.cmake cmake/zzlogg_brand.h.in src/settings/CMakeLists.txt src/settings/src/persistentinfo.cpp tests/ui2
git commit -m "feat: 建立 ZzLogg 品牌与配置身份"
```

### 任务 2：制作可复现的 ZzLogg 图标资产

**文件：**
- 移动/修改：`src/app/images/hicolor/scalable/klogg.svg` → `src/app/images/hicolor/scalable/ZzLogg.svg`
- 移动/生成：`src/app/images/hicolor/*/ZzLogg.png`
- 移动/生成：`Resources/ZzLogg.ico`、`Resources/ZzLogg.icns`
- 创建：`tools/CMakeLists.txt`
- 创建：`tools/icon_generator/CMakeLists.txt`
- 创建：`tools/icon_generator/main.cpp`
- 修改：`CMakeLists.txt`
- 修改：`src/app/klogg.qrc:7-9`
- 创建：`tests/ui2/iconassettest.cpp`
- 修改：`tests/ui2/CMakeLists.txt`

- [ ] **步骤 1：先写图标资产失败测试**

测试读取源码树资产并验证以下硬约束：

```cpp
QVERIFY( svg.contains( QByteArrayLiteral( "id=\"zzlogg-log-lines\"" ) ) );
QVERIFY( svg.contains( QByteArrayLiteral( "id=\"zzlogg-handle\"" ) ) );
QVERIFY( svg.contains( QByteArrayLiteral( "id=\"zzlogg-z\"" ) ) );
QVERIFY( !svg.contains( QByteArrayLiteral( "<text" ) ) );

const QList<int> pngSizes{ 16, 32, 48, 64, 128, 256, 512 };
for ( int size : pngSizes ) {
    QImage image( QStringLiteral( ZZLOGG_HICOLOR_ROOT "/%1x%1/ZzLogg.png" ).arg( size ) );
    QCOMPARE( image.size(), QSize( size, size ) );
    QVERIFY( image.hasAlphaChannel() );
}
```

ICO 解析断言帧集合为 `{16,20,24,32,40,48,64,128,256}`；ICNS 解析断言
16、32、64、128、256、512、1024 px 表示对应的 chunk 集合包含
`icp4, icp5, icp6, ic07, ic08, ic09, ic10`。QRC 图标通过
`QIcon(QString::fromLatin1(zzlogg::brand::IconResource))` 加载且非空。

图标测试 target 必须把 `src/app/klogg.qrc` 作为 source 并启用 `AUTORCC`，否则
测试进程不会包含应用资源：

```cmake
add_executable(zzlogg_icon_asset_test
  iconassettest.cpp "${PROJECT_SOURCE_DIR}/src/app/klogg.qrc")
target_link_libraries(zzlogg_icon_asset_test PRIVATE
  zzlogg_brand Qt6::Test Qt6::Gui Qt6::Svg)
set_target_properties(zzlogg_icon_asset_test PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_definitions(zzlogg_icon_asset_test PRIVATE
  ZZLOGG_HICOLOR_ROOT="${PROJECT_SOURCE_DIR}/src/app/images/hicolor")
add_test(NAME zzlogg_ui2.icon_assets
  COMMAND zzlogg_icon_asset_test -platform offscreen)
```

- [ ] **步骤 2：运行图标测试确认旧资源红灯**

运行：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug --target zzlogg_icon_asset_test
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug -R 'zzlogg_ui2\.icon_assets' --output-on-failure
```

预期：测试因 `ZzLogg.svg`、新增 PNG 尺寸和 ZzLogg ICO/ICNS 不存在而失败。

- [ ] **步骤 3：把批准的视觉结构写入 SVG 母版**

保留原 SVG 的 RDF 许可/作者元数据和 `Stack`、`Glass` 层。新增元素使用固定路径与稳定 ID：

```xml
<g id="zzlogg-log-lines" opacity="0.92">
  <rect x="9.5625" y="7.125" width="17.53125" height="1.78125" rx="0.28125" fill="#3b82f6"/>
  <rect x="7.6875" y="10.96875" width="7.03125" height="1.6875" rx="0.28125" fill="#ef4444"/>
  <rect x="9.5625" y="14.8125" width="5.34375" height="1.6875" rx="0.28125" fill="#f59e0b"/>
  <rect x="9.5625" y="18.75" width="5.34375" height="1.6875" rx="0.28125" fill="#22c55e"/>
  <rect x="9.5625" y="22.59375" width="4.96875" height="1.6875" rx="0.28125" fill="#8b5cf6"/>
</g>
```

把手组放在 `Glass` 内、原把手之后、镜框圆形之前，保证连接处仍由镜框覆盖：

```xml
<g id="zzlogg-handle" stroke-linecap="round">
  <path d="M 29.1,30.2 43.5,41.6" fill="none" stroke="#9a5b16" stroke-width="5.2" opacity="0.78"/>
  <path d="M 29.1,30.2 43.5,41.6" fill="none" stroke="#f2a735" stroke-width="3.7" opacity="0.82"/>
  <path d="M 29.7,29.7 42.6,40.1" fill="none" stroke="#ffe0a3" stroke-width="0.95" opacity="0.62"/>
</g>
```

小写 `z` 放在镜片内、高光路径之前，使用固定轮廓而不是 `<text>`：

```xml
<path id="zzlogg-z"
      d="M 12.2,10.8 H 24.6 V 13.7 L 17.1,21.7 H 24.8 V 24.8 H 11.8 V 22.0 L 19.4,13.9 H 12.2 Z"
      fill="#275fa8" stroke="#eef6ff" stroke-width="0.5" stroke-linejoin="round"/>
```

在 512 px 预览中确认 `z` 视觉中心与镜片中心一致且相对上一版上移；不要把根目录 `serach.*` 合入图标。

- [ ] **步骤 4：实现显式图标生成器**

顶层新增默认关闭选项：

```cmake
option(ZZLOGG_BUILD_ICON_TOOL "Build the ZzLogg icon asset generator" OFF)
if(ZZLOGG_BUILD_ICON_TOOL)
  add_subdirectory(tools)
endif()
```

生成器链接 `Qt6::Core Qt6::Gui Qt6::Svg`，CLI 固定为：

```text
zzlogg_icon_generator --source <ZzLogg.svg> --hicolor-root <images/hicolor> --ico <Resources/ZzLogg.ico> --icns <Resources/ZzLogg.icns>
```

`tools/icon_generator/CMakeLists.txt` 自己执行
`find_package(Qt6 REQUIRED COMPONENTS Core Gui Svg)`。`main.cpp` 使用 `QSvgRenderer`
渲染透明 ARGB32 图像。ICO 使用小端 ICONDIR/ICONDIRENTRY 并嵌入各尺寸 PNG
字节；ICNS 写入大端 `icns` 总长度与
`icp4/icp5/icp6/ic07/ic08/ic09/ic10` PNG chunk。先在临时目录完成全部渲染与
容器编码，只有全部成功才逐一提交到目标路径；渲染或编码失败时不触碰现有派生
资源并返回非零。

- [ ] **步骤 5：生成并接入全部派生资产**

运行：

```powershell
$env:CMAKE_PREFIX_PATH = 'D:\SoftWare\Qt\6.11.0\msvc2022_64'
& 'D:\SoftWare\CMake\bin\cmake.exe' --preset windows-vs2026 -DZZLOGG_BUILD_ICON_TOOL=ON -DKLOGG_USE_HYPERSCAN=OFF
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-debug --target zzlogg_icon_generator
& '.\out\build\windows-vs2026\output\Debug\zzlogg_icon_generator.exe' --source '.\src\app\images\hicolor\scalable\ZzLogg.svg' --hicolor-root '.\src\app\images\hicolor' --ico '.\Resources\ZzLogg.ico' --icns '.\Resources\ZzLogg.icns'
```

`klogg.qrc` 使用别名，避免运行时依赖磁盘文件名：

```xml
<file alias="zzlogg/icons/ZzLogg.svg">images/hicolor/scalable/ZzLogg.svg</file>
<file alias="zzlogg/icons/16x16/ZzLogg.png">images/hicolor/16x16/ZzLogg.png</file>
<file alias="zzlogg/icons/32x32/ZzLogg.png">images/hicolor/32x32/ZzLogg.png</file>
<file alias="zzlogg/icons/48x48/ZzLogg.png">images/hicolor/48x48/ZzLogg.png</file>
```

- [ ] **步骤 6：运行图标测试和小尺寸人工检查**

运行：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --preset windows-vs2026-ui2 -DKLOGG_USE_HYPERSCAN=OFF
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug --target zzlogg_icon_asset_test
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug -R 'zzlogg_ui2\.icon_assets' --output-on-failure
```

预期：资产测试通过。随后打开 16、32、48、64、256、512 px PNG，逐个确认：`z` 可辨识、没有字体替换、日志块不越过纸张、镜框与高光覆盖 `z`、把手连接处不露出直线端点。

- [ ] **步骤 7：提交图标母版、生成器和派生资产**

```powershell
git add CMakeLists.txt tools src/app/klogg.qrc src/app/images/hicolor Resources tests/ui2
git commit -m "feat: 更新 ZzLogg 跨平台应用图标"
```

### 任务 3：统一运行时品牌并关闭无效更新源

**文件：**
- 创建：`src/app/zzloggapplicationidentity.h`
- 创建：`src/app/zzloggapplicationidentity.cpp`
- 修改：`src/app/CMakeLists.txt:36-73`
- 修改：`src/app/applicationrunner.cpp:506-647`
- 修改：`src/app/cli.h:52-137`
- 修改：`src/app/kloggapp.h:65-334`
- 修改：`src/ui/src/mainwindow.cpp:123-195,983-1248,1650-1838,2108-2261`
- 修改：`src/ui/src/mainwindowtext.cpp:26-88`
- 修改：`src/versioncheck/include/versionchecker.h`
- 修改：`src/versioncheck/src/versionchecker.cpp:45-129`
- 修改：`src/crash_handler/include/issuereporter.h`
- 修改：`src/crash_handler/src/issuereporter.cpp:33-116`
- 修改：`tests/ui2/brandcontracttest.cpp`
- 修改：`tests/ui2/runtimecontracttest.cpp`
- 修改：`src/app/i18n/en.ts`、`src/app/i18n/zh_CN.ts`、`src/app/i18n/zh_TW.ts`
- 删除：`latest.json`

- [ ] **步骤 1：先扩展运行时失败测试**

在 `runtimecontracttest.cpp` 的 `KloggApp` 创建后断言：

```cpp
QCOMPARE( app.applicationName(), QStringLiteral( "ZzLogg" ) );
QCOMPARE( app.applicationDisplayName(), QStringLiteral( "ZzLogg" ) );
QCOMPARE( app.organizationName(), QStringLiteral( "JackfahdinQt" ) );
QVERIFY( !app.windowIcon().isNull() );
```

新增窗口后断言窗口图标非空；`brandcontracttest.cpp` 断言 `VersionChecker::isUpdateCheckConfigured()` 为 `false`，并断言 `IssueReporter::issueUrl(IssueTemplate::Bug)` 的 scheme/host/path：

```cpp
const QUrl issueUrl = IssueReporter::issueUrl( IssueTemplate::Bug );
QCOMPARE( issueUrl.scheme(), QStringLiteral( "https" ) );
QCOMPARE( issueUrl.host(), QStringLiteral( "gitcode.com" ) );
QCOMPARE( issueUrl.path(), QStringLiteral( "/JackfahdinQt/ZzLogg/issues/new" ) );
```

`zzlogg_brand_contract_test` 在本任务增加 `klogg_versioncheck` 和
`klogg_crash_handler` 链接依赖。`zzlogg_runtime_contract_test` 增加
`zzloggapplicationidentity.cpp/.h` 与 `src/app/klogg.qrc` source，并启用
`AUTORCC`。

- [ ] **步骤 2：运行测试确认应用身份尚未设置**

运行：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug --target zzlogg_brand_contract_test zzlogg_runtime_contract_test
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug -R 'zzlogg_ui2\.(brand_contract|runtime_contract)' --output-on-failure
```

预期：编译先因两个可测试 API 不存在而失败；API 声明加入后，运行时身份断言仍因应用名称/图标为空而失败。

- [ ] **步骤 3：实现共享 QApplication 身份初始化**

`zzloggapplicationidentity.cpp` 把构造前身份与构造后图标分开，确保
`KDSingleApplication` 在成员构造阶段已经看到稳定应用名：

```cpp
void prepareZzLoggApplicationIdentity()
{
    QCoreApplication::setApplicationName(
        QString::fromLatin1( zzlogg::brand::ProductName ) );
    QGuiApplication::setApplicationDisplayName(
        QString::fromLatin1( zzlogg::brand::ProductName ) );
    QCoreApplication::setOrganizationName(
        QString::fromLatin1( zzlogg::brand::Vendor ) );
    QCoreApplication::setOrganizationDomain( QStringLiteral( "gitcode.com" ) );
}

bool applyZzLoggApplicationIcon( QApplication& app, QString* error )
{
    const QIcon icon( QString::fromLatin1( zzlogg::brand::IconResource ) );
    if ( icon.isNull() ) {
        if ( error )
            *error = QStringLiteral( "ZzLogg application icon could not be loaded" );
        return false;
    }
    app.setWindowIcon( icon );
    return true;
}
```

`runKloggApplication()` 在读取 `Configuration` 和构造 `KloggApp` 前调用
`prepareZzLoggApplicationIdentity()`，在每次 `appStorage.emplace(argc, argv)` 后立即调用
`applyZzLoggApplicationIcon()`；图标失败时向 stderr 写清楚资源路径并返回
`EXIT_FAILURE`，不能继续启动无图标应用。旧 UI、portable 和 UI2 都走这一个入口。

`runtimecontracttest.cpp` 自己构造 `KloggApp` 而不调用共享 runner，因此测试 `main()`
必须模拟同一顺序：先调用 `prepareZzLoggApplicationIdentity()`，再构造 `KloggApp`，
随后调用 `applyZzLoggApplicationIcon()`；图标初始化失败时返回独立非零退出码。

- [ ] **步骤 4：让所有窗口和文案使用统一身份**

`MainWindow` 不再逐个加载旧 PNG，改为：

```cpp
mainIcon_ = QApplication::windowIcon();
setWindowIcon( mainIcon_ );
scratchPad_.setWindowIcon( mainIcon_ );
scratchPad_.setWindowTitle( tr( "ZzLogg - scratchpad" ) );
```

把命令行描述、`--version` 首行、托盘提示、关于页、下载/归档/文档/崩溃消息框、窗口标题和新版本提示改为 ZzLogg。内部日志分类、命名空间、类名、临时目录前缀与版权注释保持原状。帮助菜单中没有 ZzLogg 地址可替换的上游 Discord/Telegram 动作从菜单移除，避免把用户引到另一个产品的社区。

- [ ] **步骤 5：实现静默禁用更新检查和 GitCode 问题链接**

`VersionChecker` 提供纯判断：

```cpp
bool VersionChecker::isUpdateCheckConfigured()
{
    return !QString::fromLatin1( zzlogg::brand::UpdateManifestUrl ).isEmpty();
}

void VersionChecker::startCheck()
{
    if ( !isUpdateCheckConfigured() ) {
        LOG_DEBUG << "ZzLogg update check is disabled: no manifest URL configured";
        return;
    }
    // 保留既有 deadline 和请求处理代码，URL 从品牌常量取得。
}
```

`IssueReporter::issueUrl()` 只负责构造 `https://gitcode.com/JackfahdinQt/ZzLogg/issues/new?body=...`，`reportIssue()` 再调用 `QDesktopServices::openUrl()`。删除根目录上游 `latest.json`，不创建伪 ZzLogg 发布清单。

- [ ] **步骤 6：同步翻译并运行聚焦测试**

先更新源码和三份 TS 中的用户可见源文本，再运行项目已有 `lupdate` 目标检查源文本映射：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug --target lupdate
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug --target zzlogg_brand_contract_test zzlogg_runtime_contract_test
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug -R 'zzlogg_ui2\.(brand_contract|runtime_contract|application_smoke)' --output-on-failure
```

预期：3 项测试通过，应用显示名为 ZzLogg，图标非空，测试期间不请求上游更新地址。

- [ ] **步骤 7：提交运行时品牌变更**

```powershell
git add src/app src/ui/src/mainwindow.cpp src/ui/src/mainwindowtext.cpp src/versioncheck src/crash_handler tests/ui2 latest.json
git commit -m "feat: 统一 ZzLogg 运行时品牌"
```

### 任务 4：重命名构建产物和绿色便携目录

**文件：**
- 修改：`src/app/CMakeLists.txt:55-208`
- 修改：`cmake/prepare_version.cmake:72-92`
- 创建：`tests/ui2/outputcontracttest.cmake`
- 修改：`tests/ui2/CMakeLists.txt`
- 修改：`docs/BUILD.md:130-169`

- [ ] **步骤 1：先写外部产物名失败测试**

`outputcontracttest.cmake` 接收目标路径和便携根目录：

```cmake
get_filename_component(main_name "${MAIN_APP}" NAME)
get_filename_component(portable_name "${PORTABLE_APP}" NAME)
get_filename_component(ui2_name "${UI2_APP}" NAME)
if(NOT main_name STREQUAL "ZzLogg.exe")
  message(FATAL_ERROR "Expected ZzLogg.exe, got ${main_name}")
endif()
if(NOT portable_name STREQUAL "ZzLogg_portable.exe")
  message(FATAL_ERROR "Expected ZzLogg_portable.exe, got ${portable_name}")
endif()
if(NOT ui2_name STREQUAL "ZzLogg_ui2.exe")
  message(FATAL_ERROR "Expected ZzLogg_ui2.exe, got ${ui2_name}")
endif()
if(NOT GREP_EXCLUDED_FROM_ALL)
  message(FATAL_ERROR "klogg_grep must remain EXCLUDE_FROM_ALL")
endif()
```

非 Windows 分支使用同样的基名但不带 `.exe`。注册测试时通过
`$<TARGET_PROPERTY:klogg_grep,EXCLUDE_FROM_ALL>` 传入
`GREP_EXCLUDED_FROM_ALL`，因此显式构建过 grep 后重复运行测试也不会误报。

- [ ] **步骤 2：运行产物测试确认旧文件名红灯**

运行：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug -R 'zzlogg_ui2\.output_contract' --output-on-failure
```

预期：测试报告实际文件仍为 `klogg.exe`、`klogg_portable.exe` 或 `zzlogg_ui2.exe`。

- [ ] **步骤 3：设置四个目标的 OUTPUT_NAME**

内部 target 名保持不动：

```cmake
set_target_properties(klogg PROPERTIES OUTPUT_NAME "ZzLogg" AUTORCC ON AUTOMOC ON)
set_target_properties(klogg_portable PROPERTIES OUTPUT_NAME "ZzLogg_portable" AUTORCC ON AUTOMOC ON)
set_target_properties(klogg_grep PROPERTIES OUTPUT_NAME "ZzLogg_grep" AUTORCC ON AUTOMOC ON)
if(TARGET zzlogg_ui2)
  set_target_properties(zzlogg_ui2 PROPERTIES OUTPUT_NAME "ZzLogg_ui2" AUTOMOC ON AUTORCC ON)
endif()
```

`klogg_grep` 继续 `EXCLUDE_FROM_ALL`。`prepare_version.cmake` 的 `NAME`、`ORIGINAL_FILENAME`、`COMPANY_NAME` 改用品牌变量；图标改为 `Resources/ZzLogg.ico`。

- [ ] **步骤 4：重命名绿色目录并保持部署逻辑**

只改外部目录和注释：

```cmake
set(KLOGG_PORTABLE_DIR "${KLOGG_PORTABLE_ROOT}/ZzLogg-portable")
```

内部自定义 target `klogg_portable_folder` 保持不变。`DeployPortable.cmake` 继续从 `$<TARGET_FILE:klogg_portable>` 获取实际新文件名，继续复制 Qt 插件、MSVC runtime、TBB、文档和许可。

- [ ] **步骤 5：验证默认构建、显式 grep 和绿色目录**

运行：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --preset windows-vs2026-ui2 -DKLOGG_USE_HYPERSCAN=OFF
Remove-Item -LiteralPath '.\out\ui2-vs\output\Debug\ZzLogg_grep.exe' -Force -ErrorAction SilentlyContinue
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug -R 'zzlogg_ui2\.output_contract' --output-on-failure
Test-Path '.\out\ui2-vs\output\Debug\ZzLogg_grep.exe'
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug --target klogg_grep
Test-Path '.\out\ui2-vs\output\Debug\ZzLogg_grep.exe'
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-relwithdebinfo --target klogg_portable_folder
```

预期：默认构建后的 `Test-Path` 为 `False`；显式构建后为 `True`；绿色目录为 `out/ui2-vs/portable/RelWithDebInfo/ZzLogg-portable/`，其中主程序为 `ZzLogg_portable.exe`。

- [ ] **步骤 6：更新构建文档并提交**

`docs/BUILD.md` 只改外部文件名和目录示例，明确内部构建 target 仍是 `klogg_portable_folder`，zip 不在本次发行流程中。

```powershell
git add src/app/CMakeLists.txt cmake/prepare_version.cmake tests/ui2 docs/BUILD.md
git commit -m "build: 重命名 ZzLogg 应用产物"
```

### 任务 5：更新平台包和公开文档，移除失效发行入口

**文件：**
- 修改：`CMakeLists.txt:248-349`
- 移动/修改：`packaging/linux/ZzLogg.desktop`
- 移动/修改：`packaging/windows/ZzLogg.nsi`
- 修改：`packaging/osx/distribution.xml`
- 修改：`packaging/osx/dmg_setup.scpt`
- 修改：`packaging/description.txt`
- 删除：文件结构章节列出的旧包管理与 zip 脚本
- 创建：`cmake/VerifyExternalBrand.cmake`
- 修改：`README.md`
- 修改：`docs/BUILD.md`
- 修改：`docs/DOCUMENTATION.md`

- [ ] **步骤 1：先写外部品牌静态检查并确认红灯**

`VerifyExternalBrand.cmake` 读取当前发行入口，禁止以下值：

```cmake
set(forbidden_literals
  "https://github.com/variar/klogg"
  "https://raw.githubusercontent.com/variar/klogg"
  "com.github.variar.klogg"
  "Applications\\klogg.exe"
  "klogg-portable")
```

同时要求：

```cmake
set(required_literals
  "https://gitcode.com/JackfahdinQt/ZzLogg"
  "com.gitcode.jackfahdinqt.zzlogg"
  "ZzLogg.exe"
  "ZzLogg.desktop")
```

检查文件限定为 `CMakeLists.txt`、当前 NSIS、desktop、macOS distribution、README、BUILD 与 DOCUMENTATION；不扫描许可证头、内部目标名或专门的上游归属段落。把脚本注册为 `zzlogg_ui2.brand_external_files` CTest，以便现有 UI2 test preset 能选中它。

运行：

```powershell
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug -R 'zzlogg_ui2\.brand_external_files' --output-on-failure
```

预期：旧主页、应用标识、Windows 注册项和 portable 目录至少各触发一项失败。

- [ ] **步骤 2：更新 CPack、Linux desktop 与图标安装**

顶层 CMake 设置：

```cmake
set(CPACK_PACKAGE_NAME "${ZZLOGG_PRODUCT_NAME}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${ZZLOGG_PRODUCT_DESCRIPTION}")
set(CPACK_PACKAGE_VENDOR "${ZZLOGG_VENDOR}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${ZZLOGG_HOMEPAGE_URL}")
set(CPACK_PACKAGE_ICON "${ICON_FILE}")
set(CPACK_STRIP_FILES "bin/ZzLogg")
```

Linux 安装 16/32/48/64/128/256/512 PNG 和 scalable SVG，目标文件名均为 `ZzLogg.*`。`ZzLogg.desktop` 使用：

```ini
[Desktop Entry]
Name=ZzLogg
GenericName=Log file viewer
Exec=ZzLogg %F
Icon=ZzLogg
Type=Application
Comment=A smart interactive log explorer.
Terminal=false
Categories=Qt;Utility;TextTools;Development;
MimeType=text/plain;
Actions=Session;NewInstance;
```

两个 desktop action 分别调用 `ZzLogg --load-session %F` 和 `ZzLogg --multi %F`。

- [ ] **步骤 3：更新 Windows NSIS 与 macOS 元数据**

NSIS 固定 Qt 6，不再保留 Qt 5 分支；安装目录、DisplayName、快捷方式、OpenWithList、文件关联、卸载键和新配置清理全部使用 ZzLogg。NSIS 输出名为 `ZzLogg-${VERSION}-${PLATFORM}-Qt6-setup.exe`，主文件为 `release\ZzLogg.exe`，图标为 `Resources\ZzLogg.ico`。

macOS 使用：

```cmake
set(MACOSX_BUNDLE_BUNDLE_DISPLAY_NAME "ZzLogg")
set(MACOSX_BUNDLE_BUNDLE_NAME "ZzLogg")
set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.gitcode.jackfahdinqt.zzlogg")
set(MACOSX_BUNDLE_ICON_FILE "ZzLogg.icns")
```

`distribution.xml` 的 title 和 pkg-ref 同步；`dmg_setup.scpt` 定位 `ZzLogg.app`。

- [ ] **步骤 4：删除会误导用户的失效发行入口**

从版本控制删除文件结构章节列出的 AppImage、Arch、Gentoo、DEB/RPM 操作笔记、Chocolatey、Scoop、手工 release/7z 文件。理由是它们固定下载上游旧版本或 Qt 5 包，当前仓库没有对应 ZzLogg 发布地址；简单改名会产生不能使用的安装方式。保留并更新 CPack、NSIS、desktop 与 CMake 绿色目录这四条当前可验证路径。

- [ ] **步骤 5：重写当前产品文档并保留来源归属**

`README.md` 顶部使用 ZzLogg 名称、当前 GitCode 仓库、功能概览和实际构建入口；删除不存在的 ZzLogg Release/Chocolatey/Scoop/Homebrew/DEB/RPM 下载承诺。新增“来源与许可”段落，明确 ZzLogg 基于 klogg/glogg 演进，并保留上游仓库、Anton Filimonov、Nicolas Bonnefon 和 GPLv3+ 归属。

`docs/BUILD.md` 标题、克隆地址、示例目录、外部产物和绿色目录改为 ZzLogg；内部选项/target（如 `KLOGG_BUILD_UI2`、`klogg_portable_folder`）保留原名并注明这是兼容性选择。`docs/DOCUMENTATION.md` 只替换读者能看到的产品名称，不改历史说明和源码标识。

- [ ] **步骤 6：运行静态检查和 CMake 安装配置检查**

运行：

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --preset windows-vs2026-ui2 -DKLOGG_USE_HYPERSCAN=OFF
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug -R 'zzlogg\.brand_external_files' --output-on-failure
rg -n -i 'github\.com/variar/klogg|raw\.githubusercontent\.com/variar/klogg|com\.github\.variar\.klogg|klogg-portable' CMakeLists.txt README.md docs packaging src/app src/ui src/versioncheck src/crash_handler
```

预期：CTest 通过；`rg` 只允许命中许可证/来源归属或内部技术说明，不能命中当前下载、更新、安装、窗口文案和包元数据。

- [ ] **步骤 7：提交平台包与文档**

```powershell
git add -A CMakeLists.txt cmake packaging AppImageBuilder.yml README.md docs
git commit -m "build: 更新 ZzLogg 平台包与文档"
```

### 任务 6：执行完整回归、绿色目录和发布前检查

**文件：**
- 修改：仅限本任务验证发现的品牌回归文件
- 修改：`docs/BUILD.md`（只记录真实主机尚未执行的验收项或实际验证结果）

- [ ] **步骤 1：记录真实配置基线并确认测试隔离**

运行：

```powershell
$oldConfig = 'C:\Users\zz\AppData\Roaming\klogg\klogg.ini'
$newConfig = 'C:\Users\zz\AppData\Roaming\ZzLogg\ZzLogg.ini'
$oldBefore = if (Test-Path -LiteralPath $oldConfig) { Get-FileHash -Algorithm SHA256 -LiteralPath $oldConfig }
$newBefore = Test-Path -LiteralPath $newConfig
$oldBefore
$newBefore
```

预期：记录旧配置哈希；不启动使用真实配置目录的应用。完成 CTest 后旧哈希必须不变，且若新配置原本不存在，测试不能创建它。

- [ ] **步骤 2：运行 Windows Debug 完整工作流**

```powershell
$env:CMAKE_PREFIX_PATH = 'D:\SoftWare\Qt\6.11.0\msvc2022_64'
& 'D:\SoftWare\CMake\bin\cmake.exe' --preset windows-vs2026-ui2 -DKLOGG_USE_HYPERSCAN=OFF
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-debug
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-debug --output-on-failure
```

预期：配置、构建和全部 UI2/品牌测试通过，0 项失败；默认输出目录不存在 `ZzLogg_grep.exe`。

- [ ] **步骤 3：运行 Windows RelWithDebInfo 完整工作流**

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-relwithdebinfo
& 'D:\SoftWare\CMake\bin\ctest.exe' --preset windows-vs2026-ui2-relwithdebinfo --output-on-failure
```

预期：RelWithDebInfo 构建和全部 UI2/品牌测试通过，0 项失败。

- [ ] **步骤 4：生成并核对绿色目录**

```powershell
& 'D:\SoftWare\CMake\bin\cmake.exe' --build --preset windows-vs2026-ui2-relwithdebinfo --target klogg_portable_folder
$portable = '.\out\ui2-vs\portable\RelWithDebInfo\ZzLogg-portable'
Get-ChildItem -LiteralPath $portable -Recurse | Select-Object FullName
Test-Path -LiteralPath "$portable\ZzLogg_portable.exe"
Test-Path -LiteralPath "$portable\platforms\qwindows.dll"
Test-Path -LiteralPath "$portable\imageformats\qsvg.dll"
```

预期：三项 `Test-Path` 都为 `True`；目录还包含 Qt 6 运行库、TBB、MSVC
runtime、COPYING、NOTICE、README 和 DOCUMENTATION，不生成 zip。

- [ ] **步骤 5：核对 Windows 版本资源和依赖边界**

```powershell
(Get-Item '.\out\ui2-vs\output\RelWithDebInfo\ZzLogg.exe').VersionInfo |
  Format-List ProductName,FileDescription,CompanyName,OriginalFilename
(Get-Item '.\out\ui2-vs\output\RelWithDebInfo\ZzLogg_ui2.exe').VersionInfo |
  Format-List ProductName,FileDescription,CompanyName,OriginalFilename
```

预期：ProductName/FileDescription 为 ZzLogg 或 `ZzLogg log viewer`，CompanyName 为 `JackfahdinQt`。用 Visual Studio 的 `dumpbin /dependents` 检查：`ZzLogg.exe` 不依赖 ZzPureTools DLL；`ZzLogg_ui2.exe` 只依赖 UI2 实际使用的拆分 DLL，不依赖聚合 `ZzPureTools.dll`。

- [ ] **步骤 6：核对真实配置、Git 差异和进程**

```powershell
$oldAfter = if (Test-Path -LiteralPath $oldConfig) { Get-FileHash -Algorithm SHA256 -LiteralPath $oldConfig }
if ($oldBefore -and $oldAfter.Hash -ne $oldBefore.Hash) { throw 'Old klogg config changed during tests' }
if (-not $newBefore -and (Test-Path -LiteralPath $newConfig)) { throw 'Tests created real ZzLogg config' }
git diff --check master...HEAD
git status --short
git submodule status
Get-Process ZzLogg,ZzLogg_portable,ZzLogg_ui2 -ErrorAction SilentlyContinue
```

预期：真实配置未变化；`git diff --check` 无输出；子模块仍固定在提交记录；没有残留应用进程；工作区只包含明确审查中的变更。

- [ ] **步骤 7：执行 Windows 人工图标验收并记录跨平台边界**

使用临时 APPDATA 启动三个 GUI 目标，检查主窗口标题、标题栏图标、任务栏图标、Alt+Tab 图标和关于页。Windows 显示缩放分别检查 100%、125%、150%、200%，并在浅色、深色和高对比度模式下检查 16/32/48 px 可读性。每次退出后确认没有残留进程。

macOS Dock/Finder 与 Linux desktop/应用菜单只能在对应真实主机确认；如果当前没有这些主机，`docs/BUILD.md` 明确写为“未在真实主机执行”，不能写成通过。

- [ ] **步骤 8：提交验证中产生的最后修正**

若验证没有产生修正，不创建空提交。若有修正：

```powershell
git add -A -- CMakeLists.txt cmake src tests packaging docs README.md Resources tools
git commit -m "test: 完善 ZzLogg 品牌验收"
```

- [ ] **步骤 9：进入完成分支流程**

使用 `superpowers:verification-before-completion` 重新读取最新输出，再使用 `superpowers:requesting-code-review` 审查规格覆盖、图标资产、配置隔离和平台包。审查问题清零后使用 `superpowers:finishing-a-development-branch`，向用户提供线性 fast-forward 合并、保留分支或放弃分支选项；未经用户明确要求不推送远端。
