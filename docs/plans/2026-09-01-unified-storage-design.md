# ZzLogg 统一存储与单 GUI 设计

## 背景

ZzLogg 目前通过不同可执行目标决定持久数据的位置：普通 GUI 使用用户配置目录，便携 GUI 使用程序目录，实验性的 grep 目标也强制使用便携配置。UI2 又作为单独的 GUI 目标存在。这样的目标矩阵增加了构建、测试、打包和用户理解成本。

本设计把“数据保存在哪里”从编译时差异改为运行时选择，并将 UI2 作为唯一正式 GUI。相同的 `ZzLogg.exe` 既可安装使用，也可放在任意文件夹中绿色使用。

## 目标

- 正式发布只包含一个 GUI 可执行文件 `ZzLogg.exe`，并使用现有 UI2 外壳。
- 首次启动在读取任何用户配置之前选择存储位置。
- 支持系统用户目录、程序目录和自定义目录三种模式。
- 设置中可以查看和更改存储位置；更改后安全迁移并在重启后生效。
- 配置、会话、运行日志和崩溃转储全部跟随所选目录。
- 旧普通版和旧便携版的数据可以无损迁移。
- 程序目录模式不在主机用户目录留下业务数据或定位信息。
- 保留实验性 grep 源码目标，但不默认构建、不进入发布包，也不弹出 GUI 向导。

## 非目标

- URL 下载、剪贴板内容和压缩包解压产生的短期工作文件继续使用系统临时目录。
- 首版不支持运行中热切换存储后继续使用；切换必须重启。
- 不自动合并两套非空配置，不把自定义目录当作多配置 profile 系统。
- 不迁移系统临时目录里历史运行留下的旧日志。
- 本设计不引入云同步、加密或跨设备同步。

## 可执行目标与发布形态

正式 GUI 目标继续使用内部目标名 `klogg`，输出 `ZzLogg.exe`，入口采用当前 `main_ui2.cpp` 的 UI2 初始化流程并链接 `zzlogg_ui2_runtime`。

移除以下正式 GUI 差异：

- 删除 `klogg_portable` / `ZzLogg_portable.exe`。
- 删除 `KLOGG_PORTABLE` 编译时路径分支。
- 删除单独的 `zzlogg_ui2` / `ZzLogg_ui2.exe`，UI2 直接成为 `ZzLogg.exe`。
- 删除只为多个 GUI 文件服务的版本资源、输出契约和安装契约。

可以继续提供安装包或免安装运行文件夹，但两者复制的是同一个 `ZzLogg.exe` 和同一组依赖。运行文件夹打包目标改为以 `ZzLogg.exe` 为输入，不再生成另一份便携可执行文件。

`klogg_grep` 保持 `EXCLUDE_FROM_ALL`。它可显式构建用于开发和诊断，但不属于正式发布产物。

## 数据目录结构

解析出的存储根目录称为 `dataRoot`。所有持久业务数据放在以下固定结构：

```text
dataRoot/
  config/
    ZzLogg.ini
  session/
    ZzLogg_session.ini
  logs/
    ZzLogg_<timestamp>_<pid>.log
  crashes/
    ... crash handler database and dumps ...
  storage-manifest.ini
```

`storage-manifest.ini` 标识该目录由 ZzLogg 管理，并记录布局版本。迁移和目录校验依赖此文件，不根据目录名猜测。

三种模式的默认 `dataRoot`：

- 用户目录：`QStandardPaths::AppDataLocation`。
- 程序目录：`<applicationDir>/data`。
- 自定义目录：用户选择的绝对规范路径，不额外偷偷追加子目录；选择界面显示最终路径预览。

## 定位文件与解析优先级

自定义路径存在启动前定位问题，因此引入只保存路径和迁移状态的轻量 locator。locator 不保存主题、历史、会话、日志或崩溃数据。

- 程序目录模式：`<applicationDir>/ZzLogg.storage.ini`。
- 用户目录和自定义目录模式：`QStandardPaths::AppConfigLocation/storage.ini`。

程序目录模式只使用相邻 locator，从而把整个运行文件夹复制到另一台电脑后仍保持绿色语义，不在主机用户目录创建 locator。

启动解析顺序固定为：

1. 显式命令行 `--data-dir <absolute-path>`；只覆盖本次进程，不改 locator。
2. 程序旁的 `ZzLogg.storage.ini`。
3. 系统 AppConfigLocation 中的 `storage.ini`。
4. 旧版配置探测与一次性迁移。
5. GUI 首次启动向导；grep 则使用用户目录默认值且不写入“向导已完成”状态。

相对路径只允许出现在程序旁 locator 中，并且始终相对可执行文件目录解析。其他来源必须保存绝对规范路径。

## 启动流程

应用启动顺序调整为：

1. 设置应用标识和 Qt 启动属性。
2. 构造 `KloggApp`，使对话框和 `QStandardPaths` 可用，但不读取 `Configuration`。
3. `StorageLocationResolver` 读取 locator、命令行覆盖和旧版数据。
4. 如果是新 GUI 安装且没有旧数据，显示首次启动存储向导。
5. 校验目录；如存在待迁移事务，在任何 `PersistentInfo` / `Persistable` 单例初始化前执行迁移。
6. 安装解析结果到进程级 `StorageContext`。
7. 初始化 `Configuration`、语言、日志、崩溃处理和窗口。

首次启动向导必须选择一种模式才能继续。用户取消时应用直接退出，不创建默认配置。向导显示每种模式的实际路径、用途和写权限状态。

## 目录校验

接受目录前必须完成：

- 路径规范化并拒绝空路径、文件路径和相对路径。
- 创建目录或确认目录可读写。
- 在目标中创建临时文件、写入、同步、读取并原子重命名，然后删除。
- 程序目录模式额外验证相邻 locator 和 `data` 子目录都可写。
- 非空目录只有在包含兼容的 `storage-manifest.ini` 时才能作为现有 ZzLogg 数据目录加载；否则要求用户选择空目录或新目录。

网络盘、移动盘或同步目录允许使用，但每次启动都重新验证。路径暂时不可用时不回退到其他目录，避免用户误以为数据丢失；应用显示恢复对话框，允许重试、重新选择或退出。

## 旧数据识别与迁移

旧版来源包括：

- 普通版：系统用户设置目录中的 `ZzLogg.ini` 与 `ZzLogg_session.ini`。
- 便携版：程序旁的 `ZzLogg.conf` 与 `ZzLogg_session.conf`。
- 启用崩溃收集时的旧 `klogg_dump` 目录。

没有 locator 但发现旧数据时，不再显示全新的首次安装向导：

- 只发现普通版数据：选择用户目录模式并迁移。
- 发现程序旁便携数据：沿用旧行为优先级，选择程序目录模式并迁移。
- 两种数据同时存在：沿用旧代码的相邻便携配置优先级，同时在迁移确认页明确显示采用的来源和目标。

迁移采用“复制、验证、切换”事务：

1. 通过 `QLockFile` 获取 locator 级迁移锁。
2. 在 locator 写入 `pending` 事务，包含源、目标、模式和事务 ID。
3. 使用 `QSaveFile` 将设置和会话写到目标布局；复制日志和崩溃数据时保留文件名。
4. 打开目标 INI，检查可读性、版本键和 `QSettings::status()`。
5. 最后写入 `storage-manifest.ini`。
6. 原子更新目标 locator 为 `active`，再清除源 locator 的 `pending`。
7. 使用目标数据成功启动并同步一次设置后，将事务标记为 `verified`。

locator 切换是提交点。提交点前发生错误时继续使用源目录；提交点后启动失败时，根据事务记录回滚到源 locator。源数据至少保留到一次成功启动并验证完成，不在迁移过程中删除。

## 设置页面与切换流程

Preferences 新增独立的“存储”页，包含：

- 当前模式和实际 `dataRoot`。
- “打开目录”按钮。
- 用户目录、程序目录、自定义目录选择。
- 自定义目录浏览按钮和最终路径预览。
- 目录校验结果。

点击应用时不热切换当前 `PersistentInfo`。设置页把新位置写为 locator 的 `pending` 事务，并提示“立即重启”或“稍后重启”。下次启动在读取配置前迁移。

从程序目录切到其他模式时，迁移成功后写入系统 locator，再删除相邻 locator；从其他模式切到程序目录时，迁移成功后写入相邻 locator，再清理系统 locator 的活动记录。任何 locator 删除失败都视为迁移未完成，避免两个 locator 同时指向不同活动目录。

## 统一路径服务

新增不可变的进程级 `StorageContext`，由启动解析器安装一次，向各子系统提供：

- `configFilePath()`
- `sessionFilePath()`
- `logsDirectory()`
- `crashesDirectory()`
- `dataRoot()`
- `mode()`

`PersistentInfo` 直接使用配置和会话文件路径，不再根据 `ForcePortable` 或相邻 `.conf` 自行决定模式。文件日志从系统临时目录改到 `logsDirectory()`；CrashHandler 删除 `KLOGG_PORTABLE` 分支并使用 `crashesDirectory()`。临时工作文件不使用 `StorageContext`。

## grep 行为

grep 不显示首次启动向导。路径规则为：

- 有 `--data-dir` 时使用该目录。
- 否则使用已有 locator。
- 没有 locator 时读取用户目录中的默认配置；如果配置不存在，使用内置默认值。

grep 不创建 GUI 首次启动完成标记，也不强制创建程序旁配置。它继续保持非默认、非发布目标。

## 错误处理

- 首次选择失败：停留在向导中并显示具体路径和系统错误。
- 活动目录丢失：显示恢复对话框，不静默创建空配置。
- 迁移复制失败：保留源目录和源 locator，删除带事务 ID 的未完成目标文件。
- 目标设置不可读：拒绝切换并报告具体文件。
- locator 损坏：保留原文件副本并进入恢复对话框，不猜测自定义路径。
- 多实例迁移冲突：只有持有 `QLockFile` 的实例可以迁移，其他实例提示已有 ZzLogg 正在切换数据位置。

## 测试策略

- 单元测试：locator 优先级、路径规范化、目录布局、程序目录相对路径和损坏 locator。
- 迁移测试：普通版、便携版、两个来源并存、复制失败、验证失败、回滚和重复启动幂等性。
- 设置测试：模式切换、不可写目录、非空非 ZzLogg 目录、pending 状态和重启提示。
- 集成测试：首次启动选择三种模式后，配置、会话、日志和 crash 路径全部位于目标根目录。
- 输出契约：正式 GUI 输出只有 `ZzLogg.exe`；不存在 `ZzLogg_portable.exe` 和 `ZzLogg_ui2.exe`；grep 不在默认构建中。
- 打包测试：安装和运行文件夹使用同一个 `ZzLogg.exe`。
- 跨平台测试：Windows、Linux、macOS 的默认目录和不可写目录处理。

## 验收条件

- 新用户首次打开 GUI 必须完成存储位置选择。
- 选择程序目录后，复制整个运行文件夹仍可携带全部持久数据运行。
- 选择自定义目录后，除轻量 locator 外，所有持久业务数据只出现在所选目录。
- 更改目录失败不会丢失或覆盖源数据。
- 旧普通版和便携版升级后能恢复原有设置、会话、高亮和历史。
- 启用日志和崩溃收集后，输出位于统一数据根目录。
- 默认构建和发布只生成一个 GUI 可执行文件。

