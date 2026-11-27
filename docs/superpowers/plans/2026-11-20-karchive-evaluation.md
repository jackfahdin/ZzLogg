# P13 官方 KArchive 迁移评估

> 沿用 writing-plans／executing-plans 内联执行，最终一次独立审查。

**目标／规格：** 用户继续已说明的官方 KArchive 兼容性和依赖成本评估。判断原版 6.31.0 是否能减少维护面并保持已验证行为，先给可复现证据；本轮不替换生产依赖、不推送，本地中文提交。

**方案：** KDE 官方发布包固定版本和 SHA-256，放在忽略的 out 目录；独立 CMake 项目复用真实 Decompressor、preparePackage 和已有四个测试源码，用同一测试宿主分别编译当前 fork 与原版 KF6。仅屏蔽诊断日志输出，不替换解压或安装逻辑。官方默认依赖及关闭不需要功能后的最小依赖分别记录；系统 codec 与内嵌 codec 版本不同，结果是整个候选栈的兼容性结论，不宣称单变量性能或缺陷归因。原版失败保持失败，不移植补丁后称原版通过。

**文件：** 新建 tests/experiments/karchive/CMakeLists.txt、logging_stub.cpp、README.md；现有测试源和生产构建不改。本文和路线图记录源码身份、构建参数、测试结果、API／取消与打包差异、采用建议。

## 审查重点

- 复用真实生产调用和固定预期；fork 控制组必须全绿，候选失败不能被包装成成功。
- 构建确实链接目标库，没有误混旧的静态库／头文件；来源归档 hash 和编译配置可追溯。
- 官方 copyTo 缺少中途取消不能被事后返回 false 掩盖；区分源码检查与实际动态验证。
- OpenSSL／zstd 的 WITH 开关只是 required/recommended 策略，不能误认为 OFF 一定不链接；实际缓存及链接确认最小依赖。
- 系统 codec 更新、Linux-only、未跑完整应用／性能／安全测试的边界如实记录。

### Task 1: 可复现官方候选验证

- [x] 获取固定版本源包，核对 API、构建依赖及本地补丁对应实现。Expected: 有版本／摘要和差异清单。
- [x] 独立宿主复用四个真实测试及 16 项流审计；当前 fork 控制组先通过，再运行未改动官方栈。Expected: 所有返回码和失败场景原样保存，兼容性差异明确。
- [x] 独立审查、核对实际链接与最小依赖，更新路线图与结论并中文提交。Expected: 不修改默认构建，不把候选缺口记作采用完成。

最终任务验证：`ctest --test-dir out/benchmarks/p13-karchive-evaluation/build-fork --output-on-failure`；候选测试和审计作为诊断，其非零退出是评估证据，不是要弱化测试的理由。


## 结论：不直接替换当前 fork

原版 KArchive 6.31.0 能编译并接入现有调用层，但未达到当前正确性基线。它仍需携带拼接流、复制完整性、空文件目录、tar 短头部及中途取消适配，现阶段直接替换不能省掉这些维护责任。生产继续使用已有修复的 fork；本轮仅提交独立评估入口和记录。

## 来源与构建

来源：[KDE 6.31.0 官方发布包](https://download.kde.org/stable/frameworks/6.31/karchive-6.31.0.tar.xz)，SHA-256 为 `95acaf5a459593184104e1c157cef415ec59a33e71674186169e0df33cdacaac`。解包后将 218 个普通文件逐字节与归档核对，候选源码未改动。下载及构建产物只保存在忽略目录 `out/benchmarks/p13-karchive-evaluation/`，不再向 vendor 添加一份代码。

本机 CMake 4.3.3、Qt 6.11.1，Release；同一独立宿主直接编译生产 `decompressor.cpp`、`package.cpp`／`transaction.cpp` 和现有四个测试。仅 logging::needLogging 固定 false，未模拟解码、输出、取消或安装策略。当前 fork 控制组使用相同日志宿主并全通过。完整复跑命令见 [实验说明](../../../tests/experiments/karchive/README.md)。

| 项目 | 当前 fork／项目 | 官方 6.31.0 候选 |
| --- | --- | --- |
| Qt／CMake 下限 | 项目 Qt 6.10／CMake 3.30 | Qt 6.10／CMake 3.30，相符 |
| C++ | 项目 C++17 | 实现使用 ranges／bit_cast，需要 C++20 |
| ECM | 语法高亮使用本地 6.31 的简化配置 | 需要完整 ECM 6.31 配置及模块 |
| codec | 内嵌 zlib 1.2.3、bzip2 1.0.5、liblzma 5.2.4 | 本机 zlib 1.3.1、bzip2 1.0.8、liblzma 5.8.3 |
| 默认额外依赖 | 没有 KArchive 引入的 OpenSSL／zstd | 默认查找 OpenSSL Crypto 与 libzstd |
| 解包取消 | copyTo 接收原子取消标志 | copyTo 无取消参数，调用结束后才检查标志 |

初次直接继承项目 C++17 编译，在 std::ranges／std::bit_cast 处失败；实验仅将 KF6Archive 目标设为 C++20，消费端仍为 C++17，未修改官方源码。语法高亮的 ECMConfig 简化配置缺少 ECM_MODULE_DIR，不能直接复用；将已有 ECM 源码完整安装到实验目录后配置成功，无系统安装。

单独默认配置实际找到了 OpenSSL **3.5.5** 和 zstd **1.5.7**。这两个 WITH 选项只改变必需／推荐级别，OFF 并不禁止 find_package。最小候选同时设置 WITH_OPENSSL=OFF／CMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE 及 WITH_LIBZSTD=OFF／CMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE，生成配置确认两种支持均为 0，保留 bzip2／xz／7z。禁用 PkgConfig 是隔离实验做法，不建议照搬为整个应用的全局配置。

检查 Ninja 链接命令和 readelf DT_NEEDED：控制组链接 libklogg_karchive.a 及三个内嵌 codec 静态库；候选链接 libKF6Archive.a 及系统 libz／libbz2／liblzma，未混入旧归档库，未直接链接 OpenSSL／zstd。默认功能配置只做依赖发现，实际兼容测试针对上述最小配置。

## 实际结果

| 同一测试／诊断 | 当前 fork | 未改动官方候选 |
| --- | --- | --- |
| Decompressor（70 项） | 通过 | 44 通过、26 失败 |
| 独立归档（18 项） | 通过 | 13 通过、5 失败 |
| 直接压缩设备（45 项） | 通过 | 18 通过、27 失败 |
| 便携更新包 | 通过 | 通过 |
| CTest 进程汇总 | 4/4，退出码 0 | 1/4，退出码 8 |
| 独立流审计 | 16/16，退出码 0 | 6/16，退出码 1 |

上述 Qt Test 数量含初始化／清理，直接设备用例中同一数据行可能产生多个失败断言，按 Qt Test 汇总计数，不累加 FAIL 行数。

实测候选仍有以下问题：

- bzip2／xz 后续成员丢失、空首成员丢失非空后续内容、坏后续成员及垃圾尾部误报成功；XZ 合法成员间 padding 和非法尾部未完整处理。旧 LZMA 单流能读，但拼接／垃圾尾部未满足严格拒绝目标。
- gzip 初始 1／5／9 字节源读取仍误判透传；直接设备的部分分块、重读及 EOF 契约未通过。
- 无修改时间属性的 7z 已不崩溃，但 solid／non-solid 空文件目录错位仍在。
- ZIP 声明长度超过真实输出、tar 仅剩 8 字节头部、真实 /dev/full 输出错误仍误报成功。

便携更新包通过不代表通用解包完整性通过：它本身使用独立清单、长度／摘要、路径与输出检查。该候选能通过此调用路径，不能替其他入口作保证。

取消差异来自 API 和源码检查，未另测中途取消延迟：官方 copyTo 不接收标志，应用现有 fallback 在完成后才返回 false；预先取消测试通过也不能证明处理中可以及时停止。官方选用较新 codec 也没有自动解决以上实测问题，错误位置与适配层源码一致；由于两组 codec 版本不同，本轮仍只对整个候选栈的兼容性下结论。

## 体积和后续取舍

按普通文件内容字节计，当前 vendor 子树为 **5,031,214 字节**，官方解包源码为 **2,303,009 字节**；二者目录所含文档／测试／翻译不同，这只是源码存放量，不能当成包体或内存节省。官方不内嵌 codec，仍需要三个外部库；Windows／自包含发布需要重新安排供应、静态或动态链接、许可证和打包验证，不能把移出 vendor 当作消除依赖。

本轮选择保留当前实现，不为变更库名额外搬运一遍已知补丁。下一步更适合做归档路径、符号链接及资源边界的专项基线，然后才评估更换归档层；底层 codec 的性能比较也应独立进行。若后续重启官方迁移，应先满足同一回归集、可中断解包及自包含打包，再决定外部 codec 的固定版本／构建方式。

本轮没有更改默认 CMake／生产源码，也没有重跑默认完整 102 项或 Python CI：验证对象是独立比较宿主的四组实际契约。没有额外 Windows／macOS、性能、全应用打包或通用归档安全结论。失败报告保留，不能将候选评估完成标为迁移采用完成。

- fork probe SHA-256：`f129113821710bd1d6b9c79768370d48842fa8dc7d1ac4415bf68fa4c7917098`；report SHA-256：`f6eafe50880198675ef542356bf856cf597e2d2093588ecb5a319b9e6285ea9e`。

- upstream probe SHA-256：`55c526009a3366e7579c36c76d5638c4d96623124bc8358204df02ba9aec7c45`；report SHA-256：`fe060a586a2942b653680edb4298c673be0bc8f2aa794be9b7ed5742eba850f7`。

构建／测试日志归档到 `out/benchmarks/p13-karchive-evaluation/validation/`，流输入／输出／报告保存在相邻 `audit-fork/`、`audit-upstream/`。


## 独立审查与保留项

一次全新上下文独立审查未发现 Critical／Important，复核了源包及 218 文件、编译标准、真实链接、测试计数和报告哈希；支持“不直接替换”的结论。

延后的 Minor：实验 README 列出了官方流审计命令，但尚未单独列出 fork 控制组的同等命令；控制组已实际执行并保存报告，说明书的成对命令补齐留作后续文档完善。

对审查未判断的跨平台／自包含打包、全应用／性能／安全、中途取消延迟及 codec 版本单独责任，维持本轮范围：这里只确认 Linux 候选栈是否满足既有调用契约，取消差异依据 API／源码；代价是这些方面仍需另行验证，不能用本轮结果替代。

后续文档完善：控制组流审计命令已在 [输出边界加固](2026-11-20-archive-boundaries.md) 一轮补入实验 README，上述 Minor 已处理。
