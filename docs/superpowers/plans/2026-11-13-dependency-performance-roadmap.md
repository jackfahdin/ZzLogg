# 依赖精简与性能优化路线

**日期：** 2026-11-13  
**分析基线：** `d48474f`  
**目标：** 优先减少外部依赖和维护复杂度，在行为兼容的前提下改善搜索、索引和解压性能。  
**架构：** 保留 Qt 桌面应用和现有日志数据模型，先清理无用依赖，再收回小而明确的功能，最后通过对照实验选择性能后端。Rust 只作为后期搜索模块的候选，不预设全项目重写。  
**技术栈：** Qt 6、C/C++；性能实验可使用 Rust，经验证后再决定是否进入正式构建。

[返回文档索引](../../README.md) · [构建指南](../../development/BUILD.md) · [CI 与发布](../../development/CI_RELEASE.md)

## 1. 如何使用这份文档

这是分阶段执行路线。表中的顺序同时考虑改动规模、行为风险、依赖关系和维护收益；阶段中的每个任务独立提交、验证、记录结果。

- “实施”任务已有明确方向，可以在完成前置检查后开始。
- “实验”任务的交付物首先是可复现的对照结果；候选没有优势时，保留现状并关闭实验也是完成。
- 每次只推进一个具体任务，不一次性删除整个第三方目录，不同时更换多个性能基础库。
- 涉及性能或搜索语义的任务，先建立基线和回归用例，再修改实现。
- 保留每次变更的提交号和前后结果，出现回归时回退该项提交。
- 推送、原生 CI 和合并按当次工作安排执行；交叉编译不等同于 Windows/macOS 原生验收。

本文是路线和验收约定。复杂实验被选中进入生产实现时，再围绕胜出的方案编写具体接口及实现计划，避免现在为所有候选维护一份推测性代码设计。

## 2. 现状与范围

以下是本次本机检查的约数，不是安装包大小，也不是性能测试结果：

| 项目 | 观察 | 含义 |
| --- | --- | --- |
| `src/` | 磁盘占用约 4.4 MB | 业务代码体积相对有限 |
| `3rdparty/` | 磁盘占用约 128 MB | 包含源码、文档、测试资源和子模块工作树 |
| `out/` | 磁盘占用约 3.4 GB | 已忽略的本地构建和下载产物；清理不会减少代码依赖 |
| `.git/` | 磁盘占用约 276 MB，其中 `.git/modules/` 约 222 MB | 包含子模块历史；删除工作树文件不会自动减少历史体积 |
| ZzPureTools 的 FluentUI 测试目录 | 磁盘占用约 24 MB | 测试资源不能直接算作正式程序的运行时负担 |
| 第一轮 5 项依赖 | Git 跟踪文件合计约 8.2 MiB | whereami、KDToolBox、maddy、ExprTk、robin-hood 的源码清理量，未扣除替代实现 |

这轮优先处理依赖和实现复杂度。清理本地缓存、重写 Git 历史、删除 UI 功能不属于默认执行范围。

## 3. 从轻到重的任务总表

| 阶段 | 任务 | 类型 | 前置条件 | 交付结果 |
| --- | --- | --- | --- | --- |
| 0 | P0 记录依赖与验证基线 | 基础工作 | 无 | 可比较的配置、行为和指标 |
| 1 | P1 移除 whereami | 实施 | P0 的构建基线 | 删除一个无实际调用的依赖 |
| 1 | P2 清理失效记录及无用打包工具 | 实施 | 逐项引用检查 | 依赖清单与实际构建一致 |
| 2 | P3 用 Qt 接管 Markdown 手册 | 实施 | P0 的手册行为记录 | 移除 maddy 和生成程序 |
| 2 | P4 收回信号节流实现 | 实施 | P0 的通知行为记录 | 移除 KDToolBox 整库依赖 |
| 3 | P5 消除搜索中的重复解码 | 实施 | 搜索基线 | 同一行只进行一次必要转换 |
| 3 | P6 实现限定布尔语法 | 实施 | 旧求值器对照测试 | 移除 ExprTk 和 robin-hood |
| 4 | P7 整理构建依赖传播与后端配置 | 实施 | 第一轮依赖变更完成 | 更明确的模块边界和构建矩阵 |
| 4 | P8 评估小型强类型实现 | 条件实施 | C++ 标准要求已梳理 | 保留类型安全，评估移除 type_safe |
| 4 | P9 用 Qt 文件监控替代 efsw | 条件实施 | 三平台文件变化用例 | Qt 监控与轮询兜底统一 |
| 4 | P10 审计 Windows TLS 运行依赖 | 条件实施 | 原生 Windows 环境 | 确认并清理多余 TLS 打包依赖 |
| 5 | P11 测试纯文本和多关键词后端 | 实验 | P5、P6、搜索基准 | 决定是否接入 SIMD／多模式搜索库 |
| 5 | P12 测试 UTF-8 正则后端 | 实验 | P11 的场景分流 | 确定正则性能与兼容回退方案 |
| 5 | P13 测试压缩后端及 KArchive 迁移 | 实验 | 压缩与更新包验收用例 | 选择上游库或解压优化 |
| 5 | P14 测试内存、编码及索引局部优化 | 实验 | 对应热点证据 | 有测量依据的局部改进 |
| 6 | P15 评估独立 Rust 搜索核心 | 架构实验 | P11/P12 证明收益 | 以批量接口集成的可选模块 |
| 6 | P16 评估流水线／索引结构重构 | 架构实验 | 性能瓶颈仍成立 | 单独设计，不预设替换 oneTBB／CRoaring |

推荐第一轮实施到 **P6**：移除五项外部依赖，并完成一次不新增依赖的搜索优化。P7 可穿插处理直接受影响的 CMake 声明，但不扩大成全仓库构建重构。

## 4. 阶段 0：建立基线

### P0 依赖、行为与性能记录

- [x] 记录起始提交、操作系统、CPU、Qt、编译器、构建类型、LTO、正则后端和线程数。
- [ ] 核对 `3rdparty/CMakeLists.txt`、各模块链接声明、`cmake/ZzPureTools.cmake`、更新器的独立依赖闭包。
- [x] 记录当前全量测试结果，区分通过、跳过、已知失败和未执行平台。
- [ ] 为即将修改的行为保存对照：三语手册、文件变化通知、搜索结果、布尔表达式错误诊断。
- [ ] 在 P5/P6 前建立可复现的搜索基准；不要求删除 whereami 前先完成所有重型性能实验。

性能记录放在被忽略的 `out/benchmarks/`；提交可复现的生成脚本、参数和结论，不提交敏感日志或大体积输入文件。报告至少记录输入字节数、行数、内容摘要、查询、结果校验值及测量环境。

搜索样本覆盖：短行、超长行、ASCII、中文、UTF-16、GBK、无效 UTF-8、CRLF/LF、末行无换行；查询覆盖单关键词、多个关键词、布尔组合、正则、忽略大小写、高命中和无命中。

测量规则：

1. 分别测量编译查询耗时、首次结果延迟、完整扫描耗时、峰值内存和取消响应。
2. 区分内存内匹配、文件读取、编码转换和结果处理，避免把 I/O 差异归因于匹配库。
3. 冷缓存和热缓存分开报告；不能控制缓存时明确标注，不声称测到了冷启动。
4. 固定构建选项和线程数，交替运行前后版本，多次采样，报告中位数及波动。
5. 先证明结果一致，再比较速度；上游 benchmark 不能作为本项目的性能结论。

## 5. 阶段 1：无行为变化的清理

### P1 移除 whereami

入口：`3rdparty/CMakeLists.txt`、`src/utils/CMakeLists.txt`、`3rdparty/vendor/whereami/`、`3rdparty/vendor/README.md`。

- [x] 再次搜索业务源码、测试、构建脚本和打包规则中的 `whereami`、`wai_get`，区分调用与依赖自身文件。
- [x] 删除 whereami 目标定义、链接项、目标清单项和 vendor 目录，更新依赖记录。
- [x] 使用新的构建目录验证配置和链接，防止旧对象文件掩盖缺失依赖。
- [x] 运行应用启动、重启路径及打包相关测试，记录提交号。

无需增加一份“断言源码中没有库名字”的测试。若检查发现实际调用，先改用适合该调用时机的 Qt 程序路径 API，再删除依赖。

### P2 清理失效条目及工具

入口：`3rdparty/vendor/README.md`、`packaging/windows/codesign_client.exe`、`.github/actions/agent-package-win/action.yml`、`cmake/StageRuntimeLicenses.cmake`。

- [x] 移除依赖表中不存在的 backward-cpp、sentry-native 等历史条目，补齐实际依赖。
- [ ] 逐项记录来源、固定版本、用途、启用条件、许可证和本地补丁；子模块和下载依赖也列入。
- [x] 核对约 13 MB 的 `codesign_client.exe` 是否仍有有效构建或人工发布用途；目前找到的工作流调用均已注释。
- [x] 确认无有效用途后删除工具和失效注释；保留现行签名脚本及仍需分发的许可证。

验收：正常构建、安装及发布路径不再引用已删除项目。此项不执行 Git 历史重写。

## 6. 阶段 2：用现有 Qt 能力替换小型依赖

### P3 移除 maddy

入口：`src/app/CMakeLists.txt`、`src/app/documentation.qrc.in`、`src/ui/src/documentationwindow.cpp`、`tests/ui_acceptance/packaging/builddocumentationcontracttest.cmake`、`tests/ui_acceptance/app/applicationtranslationtest.cpp`。

- [ ] 保存三语手册的标题、表格、代码块、图片、外链、目录跳转和主题显示行为。
- [ ] 将三语 Markdown 作为资源内嵌，使用 `QTextDocument::setMarkdown()` 或 QTextBrowser 对应接口加载。
- [ ] 保持语言切换、滚动位置恢复和可读样式，验证资源相对链接及标题锚点。
- [ ] 删除 maddy 构建目标、HTML 生成命令和 vendor 目录，同步更新构建契约测试。

验收：三语手册可读、链接可用，干净构建不再生成或执行 maddy。Qt Markdown 不兼容的语法先调整手册或保留小范围转换，不自行编写 Markdown 解析器。

### P4 收回信号节流器

入口：`src/filewatch/include/filewatcher.h`、`src/filewatch/src/filewatcher.cpp`、`src/logdata/include/logfiltereddata.h`、`src/logdata/src/logfiltereddata.cpp`、相关 CMake；新组件建议放在 `src/utils/`。

- [ ] 为当前实际使用的 trailing throttle 建立行为测试：单次通知、短时突发、持续事件、最终进度、销毁时仍有待发通知。
- [ ] 基于 QTimer 实现仅满足这些调用需求的组件，明确销毁时发出还是丢弃待发通知，并与调用方生命周期协调。
- [ ] 定时器活跃期间累计待发状态；不能在每次事件到达时不断重启定时器，把节流错误地实现成防抖。
- [ ] 替换两个调用模块，移除 KDToolBox 目标和 vendor 目录。

验收：持续追加日志时仍定期刷新；搜索结束状态能及时显示；对象销毁后无回调访问。若自有实现反而复杂，退回只保留上游 KDSignalThrottler 组件，不复制整个工具箱。

## 7. 阶段 3：搜索局部优化和布尔语法收缩

### P5 同一行只解码一次

入口：`src/regex/include/hsregularexpression.h` 的 `DefaultRegularExpressionMatcher::match()`；测试入口为 `tests/unit/patternmatcher_test.cpp`。

当前 UTF-8 → QString 转换发生在每个模式的循环内部。先保留 QRegularExpression 后端，将转换移到循环外，让同一行的所有模式复用同一 QString；零模式时不做多余转换。

- [ ] 增加多模式、中文、无效 UTF-8、空行及无匹配的结果对照。
- [ ] 先记录当前实现耗时，再移动转换位置。
- [ ] 比较单模式及多模式吞吐、分配和结果，确认没有改变 Unicode 替换语义。
- [ ] 独立提交，不同时更换正则引擎。

### P6 用限定布尔语法替代 ExprTk

入口：`src/regex/include/booleanevaluator.h`、`src/regex/src/booleanevaluator.cpp`、`src/regex/src/regularexpression.cpp`、`tests/unit/patternmatcher_test.cpp`、`src/regex/CMakeLists.txt`、`src/logdata/CMakeLists.txt`、`3rdparty/CMakeLists.txt`。

目标实现：查询创建时解析一次，生成紧凑的布尔求值指令；匹配时输入各子模式命中值。保留四个以内子模式的真值表优化，较多子模式使用预分配的执行栈，避免逐行分配。

- [ ] 将现有 ExprTk 作为迁移对照，记录合法语法、运算符优先级、结合性和错误位置。
- [ ] 覆盖 `and/or/not/nand/nor/xor/xnor`、当前接受的符号写法、括号、空白、转义引号、非法 token、缺失操作数、深层嵌套。
- [ ] 小表达式枚举全部输入真值组合；较大表达式使用固定随机种子的生成测试，核对新旧结果。
- [ ] 新实现保持现有 `BooleanExpressionEvaluator` 的调用接口；增加明确的输入长度和嵌套限制，记录与旧行为的差异。
- [ ] 删除 ExprTk；确认 robin-hood 没有实际使用后，一并删除其目标、残留 include 和 vendor 目录。
- [ ] 更新依赖记录，执行搜索功能、布尔组合和性能回归。

验收：现有有效查询的结果保持一致；新限制产生明确诊断；不因换解析器删除现有运算符。重点衡量维护和构建成本，不预设所有查询都会运行得更快。

## 8. 阶段 4：模块边界与跨平台能力

### P7 构建依赖传播及配置统一

入口：各 `src/*/CMakeLists.txt`、`CMakeLists.txt`、`CMakePresets.json`、`cmake/CompilerFlags.cmake`、`.github/workflows/ci-build.yml`。

- [ ] 根据公共头文件实际暴露的类型，逐项判断依赖应为 PUBLIC 还是 PRIVATE，优先检查 `klogg_utils` 传播的 Qt 模块。
- [ ] 梳理顶层 C++17、目标级要求和 ZzPureTools／KSyntaxHighlighting 的 C++20 要求，使文档与实际编译配置一致。
- [ ] 明确标准搜索配置和加速搜索配置，记录默认值；本地 presets 与各平台 CI 不再隐式采用不同后端。
- [ ] 保留至少一条加速后端构建／行为检查路径，或明确退出该后端的支持，不只删除验证覆盖。

### P8 type_safe

入口：`src/logdata/include/linetypes.h`、`src/utils/include/containers.h` 及其他 `type_safe::` 调用。

- [ ] 核对 OffsetInFile、LinesCount、LineNumber、LineLength、LineColumn 的构造、运算、饱和处理、元类型和输出行为。
- [ ] 评估使用少量本地结构与受检查窄化转换的维护成本；满足 C++20 配置后可使用 `std::in_range` 等标准能力。
- [ ] 保留不同单位不能混用的编译期保护，不用普通整数别名替代强类型。
- [ ] 全量编译并运行边界值测试后决定是否移除库；收益不足时保留。

### P9 efsw → Qt 监控＋轮询

入口：`src/filewatch/`、`src/settings/` 中的监控配置、`tests/ui/logdata_test.cpp`、`tests/ui_acceptance/app/applicationtranslationtest.cpp` 中的追加日志场景。

- [ ] 保持 FileWatcher 对外接口，先建立追加、截断、重命名、轮转、删除后重建、同尺寸覆盖及网络路径用例。
- [ ] 用 QFileSystemWatcher 提供通知，复用现有状态检查和轮询兜底；通知只作为重新检查的触发信号。
- [ ] 处理路径重建后的重新注册、监控失败和数量限制，保持事件合并与节流。
- [ ] 三平台实测通知延迟、空闲 CPU、持续写入时刷新频率，再决定切换默认实现和删除 efsw。

### P10 Windows TLS 依赖审计

入口：`.github/workflows/ci-build.yml`、`.github/actions/agent-package-win/action.yml`、`cmake/VerifyExternalBrand.cmake`、`src/updateqt/src/` 网络下载实现。

- [ ] 在实际打包产物中检查 Qt TLS 插件、可用后端和成功 HTTPS 连接使用的后端。
- [ ] 验证 Schannel 能否独立满足更新清单、安装包和远程日志下载需求。
- [ ] 覆盖正常证书、证书失败、重定向和代理；不通过关闭证书验证获得“成功”。
- [ ] 仅在证实不需要旧 OpenSSL DLL 后删除其下载、复制和契约断言；若必须保留 OpenSSL，则采用与 Qt 后端匹配的维护版本。

## 9. 阶段 5：有证据后引入性能库

### P11 纯文本和多关键词

| 候选 | 语言 | 实验范围 | 决策注意事项 |
| --- | --- | --- | --- |
| 标准库字节搜索 | C++ | 区分大小写的纯文本 | 先作为最小依赖方案；标准库／libc 可能已有优化 |
| [StringZilla](https://github.com/ashvardanian/StringZilla) | C/C++ | 字节、子串、分隔符扫描 | 只评估需要的 CPU 功能，避免引入无关模块 |
| [memchr](https://github.com/BurntSushi/memchr) | Rust | 单字节和子串搜索 | 通过批量接口测量，不按行跨 FFI |
| [aho-corasick](https://github.com/BurntSushi/aho-corasick) | Rust | 多关键词一次扫描 | 核对匹配模式、重叠、内存及构建成本 |

- [ ] 按模式数量、长短、命中率和文件编码测量，不用单一大字符串样本作决定。
- [ ] 快速路径仅接受已证明语义等价的输入；Unicode 忽略大小写、无效 UTF-8、空关键词和字节边界单独验证。
- [ ] 保持每行和每个子模式的命中关系，供布尔求值与结果高亮使用。
- [ ] 只有收益足以覆盖新增依赖和打包成本时才接入；不要同时引入所有候选。

### P12 UTF-8 正则和多正则

| 候选 | 语言 | 价值 | 兼容约束 |
| --- | --- | --- | --- |
| [PCRE2-8＋JIT](https://github.com/PCRE2Project/pcre2) | C | 直接处理 UTF-8，减少 QString 转换 | Qt 本身使用 PCRE2；收益来自接口和数据路径，需核对 Unicode 选项及无效编码行为 |
| [regex／regex-automata](https://github.com/rust-lang/regex) | Rust | 常规正则与字节搜索，多种匹配策略 | 不支持环视、反向引用等部分现有语法 |
| [Vectorscan](https://github.com/VectorCamp/vectorscan) | C/C++ | 大量固定正则反复扫描 | 编译耗时、内存、CPU 分派、平台构建和语法差异 |

- [ ] 先复核当前 Qt 后端的编译复用与 JIT 条件，避免拿未优化的基线比较。
- [ ] 同时测试查询构建、首次结果和重复扫描；单关键词和大量规则分别报告。
- [ ] 明确不支持语法的回退路径；搜索、布尔求值及高亮使用一致的语义。
- [ ] 验证 UTF-8／UTF-16 偏移换算、零长度匹配、锚点、大小写和逐行边界。
- [ ] 检查现有 Hyperscan/Vectorscan 接入的 x86 指令集门控及 ARM 路径，不把“库支持 ARM”等同于“应用已正确接入”。

### P13 压缩与归档

入口：`src/ui/src/decompressor.cpp`、`src/portableupdater/package.cpp`、`tests/portableupdater/packagetest.cpp`、`3rdparty/vendor/klogg_karchive/`。

- [ ] 建立 ZIP、7z、tar、gzip、bzip2、xz、损坏包、超大解压结果和恶意归档路径的回归集，同时覆盖便携更新包。
- [ ] 单独比较 [zlib-ng](https://github.com/zlib-ng/zlib-ng) 与当前 zlib 的流式解压；使用适合当前调用方式的兼容配置。
- [ ] 调查迁移官方 [KF6 KArchive](https://invent.kde.org/frameworks/karchive) 的版本要求和依赖闭包，以减少 fork 维护；备选 [libarchive](https://github.com/libarchive/libarchive)。
- [ ] 每次只更换一个层次：归档 API 或底层 codec，便于定位回归。

[libdeflate](https://github.com/ebiggers/libdeflate)仅作为明确有界的整块数据候选，其非流式 API 不直接替代超大压缩日志处理。更换归档库也不会自动消除各格式的 codec 依赖；不自行实现压缩格式解析。

### P14 内存、编码、索引局部优化

- [ ] **mimalloc：** 对照标准分配器测量打开／关闭文件、搜索、峰值及保留内存。入口为 `src/utils/include/containers.h`、`src/app/applicationrunner.cpp`；同时处理 Roaring 的分配钩子，避免分配释放器混用。
- [ ] **encoding_rs：** 使用 [encoding_rs](https://github.com/hsivonen/encoding_rs) 测试传统编码直接转 UTF-8，核对编码别名、错误替换和跨块状态；它不能替代 uchardet 的编码检测。
- [ ] **索引块复用：** 对 `src/logdata/src/compressedlinestorage.cpp` 的单点／连续访问计数，确认重复解压是否为热点，再测试小型解压块缓存；覆盖追加、截断、失效和线程访问。
- [ ] **现有性能库升级：** 分别评估 simdutf、xxHash、streamvbyte、CRoaring 的版本与实际 CPU 分派，一次升级一个库，不预设换成 Rust 就会更快。

## 10. 阶段 6：架构级变更

### P15 独立 Rust 搜索核心

仅当 P11/P12 的结果证明有足够收益时推进。候选模块内部可组合 memchr、aho-corasick、regex-automata，Qt/C++ 继续管理 UI、文件和任务。

- [ ] 定义“编译查询—扫描文本块及行边界—返回命中结果—销毁查询”的批量接口。
- [ ] 明确缓冲区生命周期、对象所有权、线程安全、取消、错误码和结果偏移单位。
- [ ] 保持现有逐行搜索语义，不能因按块扫描而意外跨行匹配；不对每行跨 FFI 调用。
- [ ] panic 和 C++ 异常不跨语言边界传播；测量包括数据交接在内的端到端性能。
- [ ] 验证 Windows MSVC、macOS 两种架构及 Linux 的编译、静态链接、运行库和发布流程。
- [ ] 对比引入 Cargo 后的干净构建耗时、安装包和依赖维护成本，记录采用或放弃的理由。

### P16 并行流水线与索引结构

oneTBB 目前承担索引、搜索和导出的有界流水线。替换为 QThreadPool、QtConcurrent 或 [Rayon](https://github.com/rayon-rs/rayon)需要重新设计任务调度、背压、取消、资源释放和线程预算。

- [ ] 先证明瓶颈来自调度／结构，而非 I/O、编码、匹配算法或自身锁顺序。
- [ ] 对一条流水线制作可切换原型，避免同时迁移索引、搜索和导出。
- [ ] 验证队列内存有界、关闭不死锁、取消及时，不叠加多个默认全核线程池。
- [ ] 只有有证据表明现有位图／偏移结构不适合实际数据分布，才启动 CRoaring 或行索引表示的替换设计。

本阶段不属于第一轮依赖精简的必选项。

## 11. 本轮保留的依赖及理由

| 依赖 | 默认决定 |
| --- | --- |
| Monocypher | 保留更新签名验证，不自行实现密码算法 |
| nlohmann-json | 保留无 Qt 更新核心所需的严格 JSON 行为，包括重复键拒绝、深度限制和数值检查 |
| KDSingleApplication | 保留单实例和更新交接的通信约定，除非单独设计协议迁移 |
| KSyntaxHighlighting | 保留语言定义与跨行状态；不以简单正则着色替代 |
| uchardet／Qt Core5Compat | 在传统编码等价迁移完成前保留 |
| CRoaring／streamvbyte／simdutf／xxHash／oneTBB | 先测量、升级或改善调用方式；无充分证据不替换 |
| ZzPureTools | 保留框架，按组件边界减少不必要构建；修改子模块时在其仓库维护，不在宿主复制另一套框架 |
| Catch2／Qt Test | 不因缩小源码目录而先重写测试体系；测试依赖不等于正式运行依赖 |

[simdjson](https://github.com/simdjson/simdjson)和 [yyjson](https://github.com/ibireme/yyjson)留作未来 JSON／NDJSON 字段过滤功能的候选。当前更新元数据规模小，替换解析器不属于日志性能优化重点；如替换，必须保留严格解析约束。

## 12. 验证、记录与当前进度

### 通用验证命令

以下采用仓库标准 Linux preset，本机 Qt 路径示例为 `/home/zz/Qt/6.11.1/gcc_64`。其他机器按[构建指南](../../development/BUILD.md)配置 Qt kit；Windows/macOS 使用对应原生 preset。

```bash
export QT_ROOT=/home/zz/Qt/6.11.1/gcc_64
cmake --preset linux-gcc-release -DWARNINGS_AS_ERRORS=ON -DKLOGG_BUILD_TESTS=ON
cmake --build --preset linux-gcc-release --parallel 6
QT_QPA_PLATFORM=offscreen ctest --preset linux-gcc-release
python3 -m unittest discover -s tests/ci
git diff --check
```

显式打开 `KLOGG_BUILD_TESTS` 是为了运行 `klogg_tests`、`klogg_itests` 中的搜索和索引用例；标准 preset 本身默认关闭这一旧测试组。具体变更先跑相关测试，阶段收尾再运行完整检查。删除依赖时至少做一次干净构建；性能对照的前后两版必须使用相同配置。

仅修改路线文档时验证链接、路径、表格和 diff，不为文档改动运行完整 C++ 构建。历史 92/92 或其他测试数量不作为未来通过标准，以当时实际注册的测试集合为准。

### 每项完成记录

在本节追加该任务的提交号、删除／增加的依赖、行为变化、测试命令及结果、性能测量环境和原生平台状态。实验结果需要明确写“采用”或“保留现状”，并解释原因。

性能候选的采用条件：输出与既定语义一致，主要收益在重复测量中超过波动，并且首次查询、内存、取消和构建成本没有不可接受的退化。小范围收益不足以抵消新增工具链时，优先选择依赖更少的方案。

### 当前状态

- [x] 完成源码依赖和候选库初步分析。
- [x] 建立按改动规模排列的路线及验收约定。
- [ ] P0：构建和测试基线已记录；手册、通知及搜索的行为／性能基线按任务补齐。
- [x] P1：移除 whereami，干净构建和 92 项 CTest 通过。
- [ ] P2：失效记录和停用工具已清理；全量依赖元数据审计仍未完成。
- [ ] P3–P4：Qt 小组件替换。
- [ ] P5–P6：搜索局部优化和布尔语法收缩。
- [ ] P7–P10：模块边界及跨平台能力评估。
- [ ] P11–P14：性能库对照实验。
- [ ] P15–P16：仅在证据支持时开展架构级实验。

**第一批结果：** 见[第一批依赖清理计划](2026-11-13-dependency-cleanup-batch1.md)。

| 项目 | 提交 | 验证与结果 |
| --- | --- | --- |
| 前置修正 | `eaec438` | 将依赖布局检查固定提交同步到 ZzPureTools `d8afb260`；修复初次基线中的 1 项失败 |
| P0 构建基线 | `0faa93a` | Linux Release、Qt 6.11.1、GCC 15.2.0、LTO ON；`ci_build` 成功，CTest 92/92；已有 `-Wodr` 警告单独保留记录 |
| P1 移除 whereami | `ca5d20d` | 新目录配置和完整构建成功，CTest 92/92；删除 26 个依赖文件、75,348 字节及构建／NOTICE 条目 |
| P2 已核实部分 | `75fdec6` | 删除 12,989,440 字节旧签名客户端及失效配置，补齐当前依赖目录和 KDE 下载依赖记录；19 项相关 CTest 通过，Python CI 124 项、3 项跳过；有效打包步骤保持一致 |

两项删除合计约 12.46 MiB 的受跟踪文件，不含其他文本变化；未测量安装包或性能收益，也未缩减 Git 历史。Windows/macOS 原生验证未执行。

**下一项：** 为 P3 保存三语手册行为，制定资源加载及链接兼容的具体迁移步骤，再使用 Qt Markdown 替换 maddy。P2 剩余的全量依赖来源、许可证和本地补丁审计继续保留为待办。
