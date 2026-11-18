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
| 初始分析的 5 项候选依赖 | Git 跟踪文件合计约 8.2 MiB | whereami、KDToolBox、maddy、ExprTk、robin-hood 的原始源码量；maddy 已决定保留，不代表最终清理量 |

这轮优先处理依赖和实现复杂度。清理本地缓存、重写 Git 历史、删除 UI 功能不属于默认执行范围。

## 3. 从轻到重的任务总表

| 阶段 | 任务 | 类型 | 前置条件 | 交付结果 |
| --- | --- | --- | --- | --- |
| 0 | P0 记录依赖与验证基线 | 基础工作 | 无 | 可比较的配置、行为和指标 |
| 1 | P1 移除 whereami | 实施 | P0 的构建基线 | 删除一个无实际调用的依赖 |
| 1 | P2 清理失效记录及无用打包工具 | 实施 | 逐项引用检查 | 依赖清单与实际构建一致 |
| 2 | P3 手册渲染依赖 | 已关闭：不替换 | 用户已决定保留 | 继续使用 maddy，不再安排替换 |
| 2 | P4 收回信号节流实现 | 已完成 | P0 的通知行为记录 | 移除 KDToolBox 整库依赖 |
| 3 | P5 消除搜索中的重复解码 | 已完成 | 搜索基线 | 同一行只进行一次必要转换 |
| 3 | P6 实现限定布尔语法 | 已完成 | 旧求值器对照测试 | 移除 ExprTk 和 robin-hood |
| 4 | P7 整理构建依赖传播与后端配置 | 已完成 | 第一轮依赖变更完成 | 更明确的模块边界和构建矩阵 |
| 4 | P8 评估小型强类型实现 | 已评估，保留 | C++ 标准要求已梳理 | 保留 type_safe，补齐类型契约回归 |
| 4 | P9 用 Qt 文件监控替代 efsw | Linux 基线与候选评估完成，保留 efsw | 三平台、网络路径及集成验证待完成 | Qt 监控与轮询兜底统一 |
| 4 | P10 审计 Windows TLS 运行依赖 | 静态审计与诊断入口完成，保留依赖 | 原生 Windows 产物验收待完成 | 确认并清理多余 TLS 打包依赖 |
| 5 | P11 测试纯文本和多关键词后端 | 完整后台任务、高亮与本机加速路由验证完成，生产保留 | 真实日志与平台准入待验证 | 决定是否接入 SIMD／多模式搜索库 |
| 5 | P12 测试 UTF-8 正则后端 | 实验 | P11 的场景分流 | 确定正则性能与兼容回退方案 |
| 5 | P13 测试压缩后端及 KArchive 迁移 | 实验 | 压缩与更新包验收用例 | 选择上游库或解压优化 |
| 5 | P14 测试内存、编码及索引局部优化 | 实验 | 对应热点证据 | 有测量依据的局部改进 |
| 6 | P15 评估独立 Rust 搜索核心 | 架构实验 | P11/P12 证明收益 | 以批量接口集成的可选模块 |
| 6 | P16 评估流水线／索引结构重构 | 架构实验 | 性能瓶颈仍成立 | 单独设计，不预设替换 oneTBB／CRoaring |

第一轮推进至 **P6**，以行为和体验不降级为前提。P3 的直接替换已发现兼容性差异，用户已决定保留 maddy、不再考虑替换，继续推进 P4–P6；不把移除五项依赖作为必须完成的数量目标。P7 可穿插处理直接受影响的 CMake 声明，但不扩大成全仓库构建重构。

## 4. 阶段 0：建立基线

### P0 依赖、行为与性能记录

- [x] 记录起始提交、操作系统、CPU、Qt、编译器、构建类型、LTO、正则后端和线程数。
- [ ] 核对 `3rdparty/CMakeLists.txt`、各模块链接声明、`cmake/ZzPureTools.cmake`、更新器的独立依赖闭包。
- [x] 记录当前全量测试结果，区分通过、跳过、已知失败和未执行平台。
- [ ] 为即将修改的行为保存对照：三语手册、文件变化通知、搜索结果、布尔表达式错误诊断。
- [x] 在 P5/P6 中建立可复现的搜索组件基准；不要求删除 whereami 前先完成所有重型性能实验。

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

### P3 已关闭：保留 maddy，不再替换

- [x] 完成三语手册的直接替换兼容性评估。
- [x] 按用户 2026-11-13 的明确决定关闭替换任务，保留 maddy 和现有 HTML 生成、资源嵌入及打包流程。

不再安排 Qt Markdown 替换、兼容适配或移除 maddy 的后续工作。历史评估保留在本页末尾，供理解这一决定使用。

### P4 收回信号节流器

入口：`src/filewatch/include/filewatcher.h`、`src/filewatch/src/filewatcher.cpp`、`src/logdata/include/logfiltereddata.h`、`src/logdata/src/logfiltereddata.cpp`、相关 CMake；新组件位于 `src/utils/`。

- [x] 为当前实际使用的 trailing throttle 建立行为测试：单次通知、短时突发、持续事件、最终进度、销毁时仍有待发通知。
- [x] 基于 QTimer 实现仅满足这些调用需求的组件，保留销毁时同步发送待发通知的原有语义，并与调用方生命周期协调。
- [x] 定时器活跃期间累计待发状态；不能在每次事件到达时不断重启定时器，把节流错误地实现成防抖。
- [x] 替换两个调用模块，移除 KDToolBox 目标和 vendor 目录。

验收：持续追加日志时仍定期刷新；搜索结束状态能及时显示；对象销毁后无回调访问。已采用仅包含尾沿节流的 Qt 组件；详见 [P4 实施与验证记录](2026-11-13-signal-throttler.md)。

## 7. 阶段 3：搜索局部优化和布尔语法收缩

### P5 同一行只解码一次

入口：`src/regex/include/hsregularexpression.h` 的 `DefaultRegularExpressionMatcher::match()`；测试入口为 `tests/unit/patternmatcher_test.cpp`。

已将 UTF-8 → QString 转换移到模式循环外，让同一行的所有模式复用同一 QString；零模式直接返回。继续使用原 QRegularExpression 后端。

- [x] 增加多模式、中文、无效 UTF-8、空行及无匹配的结果对照。
- [x] 先记录当前实现耗时，再移动转换位置。
- [x] 比较单模式及多模式吞吐、分配和结果，确认没有改变 Unicode 替换语义。
- [x] 独立提交，不同时更换正则引擎。

已采用，详见 [P5 实施与性能记录](2026-11-13-regex-decode-once.md)。语义与分配检查通过，中文多模式内存扫描获得可重复收益；单模式及 ASCII 耗时受相同源码控制偏差影响，不能保证提速或据此证明无退化。

### P6 用限定布尔语法替代 ExprTk

入口：`src/regex/include/booleanevaluator.h`、`src/regex/src/booleanevaluator.cpp`、`src/regex/src/regularexpression.cpp`、`tests/unit/patternmatcher_test.cpp`、`src/regex/CMakeLists.txt`、`src/logdata/CMakeLists.txt`、`3rdparty/CMakeLists.txt`。

目标实现：查询创建时解析一次，生成紧凑的布尔求值指令；匹配时输入各子模式命中值。保留四个以内子模式的真值表优化，较多子模式使用预分配的执行栈，避免逐行分配。

- [x] 将现有 ExprTk 作为迁移对照，记录合法语法、运算符优先级、结合性和错误位置。
- [x] 覆盖 `and/or/not/nand/nor/xor/xnor`、当前接受的符号写法、括号、空白、转义引号、非法 token、缺失操作数、深层嵌套。
- [x] 小表达式枚举全部输入真值组合；较大表达式使用固定随机种子的生成测试，核对新旧结果。
- [x] 新实现保持现有 `BooleanExpressionEvaluator` 的调用接口；增加明确的输入长度和嵌套限制，记录与旧行为的差异。
- [x] 删除 ExprTk；确认 robin-hood 没有实际使用后，一并删除其目标、残留 include 和 vendor 目录。
- [x] 更新依赖记录，执行搜索功能、布尔组合和性能回归。

验收：现有有效查询的结果保持一致；新限制产生明确诊断；不因换解析器删除现有运算符。重点衡量维护和构建成本，不预设所有查询都会运行得更快。

已采用，兼容性修复、输入边界和组件性能测量见 [P6 实施记录](2026-11-13-boolean-evaluator.md)。

## 8. 阶段 4：模块边界与跨平台能力

### P7 构建依赖传播及配置统一

入口：各 `src/*/CMakeLists.txt`、`CMakeLists.txt`、`CMakePresets.json`、`cmake/CompilerFlags.cmake`、`.github/workflows/ci-build.yml`。

- [x] 根据公共头文件实际暴露的类型，逐项判断依赖应为 PUBLIC 还是 PRIVATE，优先检查 `klogg_utils` 传播的 Qt 模块。
- [x] 梳理顶层 C++17、目标级要求和 ZzPureTools／KSyntaxHighlighting 的 C++20 要求，使文档与实际编译配置一致。
- [x] 明确标准搜索配置和加速搜索配置，记录默认值；本地 presets 与各平台 CI 不再隐式采用不同后端。
- [x] 保留至少一条加速后端构建／行为检查路径，或明确退出该后端的支持，不只删除验证覆盖。

已完成首轮公共头文件依赖审计与收紧；保留 Widgets／Core5Compat 等真实公开依赖。语言标准分层和显式后端矩阵见 [P7 实施记录](2026-11-14-build-dependencies.md)。

### P8 type_safe

入口：`src/logdata/include/linetypes.h`、`src/utils/include/containers.h` 及其他 `type_safe::` 调用。

- [x] 核对 OffsetInFile、LinesCount、LineNumber、LineLength、LineColumn 的构造、运算、饱和处理、元类型和输出行为。
- [x] 评估使用少量本地结构与受检查窄化转换的维护成本；满足 C++20 配置后可使用 `std::in_range` 等标准能力。
- [x] 保留不同单位不能混用的编译期保护，不用普通整数别名替代强类型。
- [x] 全量编译并运行边界值测试后决定是否移除库；收益不足时保留。

评估完成，保留 type_safe。本机布局无额外存储开销，局部生成代码差异不足以证明实际搜索收益；约 446 KiB 源码清理量不足以抵消自维护强类型与转换策略的成本。详见 [P8 记录](2026-11-14-strong-types.md)。

### P9 efsw → Qt 监控＋轮询

入口：`src/filewatch/`、`src/settings/` 中的监控配置、`tests/ui/logdata_test.cpp`、`tests/ui_acceptance/app/applicationtranslationtest.cpp` 中的追加日志场景。

- [x] 保持 FileWatcher 对外接口，建立追加、截断、重命名轮转、删除重建、同尺寸覆盖的 Linux 基线及实际 LogData 回归；提供网络路径测试入口。
- [x] 建立独立 QFileSystemWatcher 通知与元数据轮询候选，保留合并与重新注册；仅用于实验，未接入应用。
- [ ] 在真实网络路径运行上述场景，完成候选与生产接口、LogData 及运行中配置切换的集成验证。
- [ ] 处理路径重建后的重新注册、监控失败和数量限制，保持事件合并与节流。
- [ ] 三平台实测通知延迟、空闲 CPU、持续写入时刷新频率，再决定切换默认实现和删除 efsw。

本轮 Linux 评估完成，保留 efsw。两者常规生命周期通过，但纯轮询均存在同元数据覆盖盲区；efsw 仅原生模式在本机父目录重建场景未恢复。小样本没有证明候选整体性能更好，资源限制及三平台准入条件仍未满足。详见 [P9 评估记录](2026-11-14-filewatch-evaluation.md)。

### P10 Windows TLS 依赖审计

入口：`.github/workflows/ci-build.yml`、`.github/actions/agent-package-win/action.yml`、`cmake/VerifyExternalBrand.cmake`、`src/updateqt/src/` 网络下载实现。

- [x] 审计安装包／便携包的 TLS 部署差异，建立独立后端、证书和内容诊断入口及 Windows 隔离副本对照步骤。
- [ ] 在实际 Windows 打包产物中检查 Qt TLS 插件、可用后端、成功 HTTPS 连接使用的后端和加载模块。
- [ ] 验证 Schannel 能否独立满足更新清单、安装包和远程日志下载需求。
- [ ] 覆盖正常证书、证书失败、重定向和代理；不通过关闭证书验证获得“成功”。
- [ ] 仅在证实不需要旧 OpenSSL DLL 后删除其下载、复制和契约断言；若必须保留 OpenSSL，则采用与 Qt 后端匹配的维护版本。

本轮静态审计与 Linux 诊断工具测试完成，保留 OpenSSL 打包现状。安装包额外复制 OpenSSL 1.1 DLL，便携包使用统一 runtime；不能仅由部署规则判断实际使用的后端。Windows 原生三条下载路径仍待验收，详见 [P10 记录](2026-11-16-windows-tls-audit.md)。

## 9. 阶段 5：有证据后引入性能库

### P11 纯文本和多关键词

| 候选 | 语言 | 实验范围 | 决策注意事项 |
| --- | --- | --- | --- |
| 标准库字节搜索 | C++ | 区分大小写的纯文本 | 先作为最小依赖方案；标准库／libc 可能已有优化 |
| [StringZilla](https://github.com/ashvardanian/StringZilla) | C/C++ | 字节、子串、分隔符扫描 | 只评估需要的 CPU 功能，避免引入无关模块 |
| [memchr](https://github.com/BurntSushi/memchr) | Rust | 单字节和子串搜索 | 通过批量接口测量，不按行跨 FFI |
| [aho-corasick](https://github.com/BurntSushi/aho-corasick) | Rust | 多关键词一次扫描 | 核对匹配模式、重叠、内存及构建成本 |

- [x] 完成首轮 Qt／QString／标准库／memchr／aho-corasick 对照：147 场景、不同模式数／长度／命中率及 UTF-8 输入，另列异常编码回退和重叠／公共前缀压力场景。
- [x] 补充 StringZilla Haswell／串行内核及与 memchr 的直接对照；本轮不接入生产，见 [补充记录](2026-11-16-stringzilla-evaluation.md)。
- [ ] 扩展真实日志和文件转码／完整内存成本，避免把合成匹配器样本外推为整应用收益。
- [x] 实验候选限定非空、区分大小写的合法纯文本模式；Unicode 忽略大小写、异常 UTF-8、BOM、空关键词和字节边界完成独立对照，未接入生产。
- [x] 实验返回逐行逐模式完整位图，保留重复与重叠模式，和 Qt 对照一致。
- [x] 链接生产 RawLines／布尔求值／结果容器，完成 memchr 逐行与 128／4096 行组件对照及取消边界模型；见 [组件记录](2026-11-18-search-pipeline-evaluation.md)。
- [x] 在隔离副本中完成实际后台任务进度／取消／更新、Qt 直接回退及字节限额；见 [worker 记录](2026-11-18-search-worker-evaluation.md)。
- [x] 验证生产高亮组件的 UTF-16／tab 坐标及 Linux x86_64 Vectorscan 实际 worker 路由；见 [高亮与路由记录](2026-11-18-search-highlight-routing.md)。
- [x] 提供外部日志／查询清单、只读快照与完整结果报告，完成本机构建／CTest 日志流程验收；见 [语料入口记录](2026-11-18-search-corpus-input.md)。
- [ ] 完成代表性业务日志、其他平台／独立 Hyperscan 和应用准入验证。
- [ ] 只有收益足以覆盖新增依赖和打包成本时才接入；不要同时引入所有候选。

首轮不采用统一替换：标准库在长公共前缀压力场景约慢 5～7 倍，aho-corasick 在高重叠场景约慢 5.5 倍；memchr 的批量结果值得继续，但工具链和应用集成尚未准入。详见 [P11 首轮记录](2026-11-16-literal-search-evaluation.md)。

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
- [x] P3：已关闭；用户决定保留 maddy，不再考虑替换。
- [x] P4：已完成尾沿节流替换，移除 KDToolBox；同组行为测试及干净构建 93 项 CTest 通过。
- [x] P5：默认 Qt 后端每行解码一次，语义对照及 93 项 CTest 通过；性能限制见实施记录。
- [x] P6：限定布尔求值器，旧有效语法真值表回归通过；移除 ExprTk、robin-hood，约 7.22 MiB vendor 源码。
- [x] P7：收紧构建依赖传播，明确 C++17／20 分层和标准／加速配置；96 项 CTest 及 Vectorscan 独立验证通过。
- [x] P8：强类型契约与生成代码评估完成，保留 type_safe。
- [ ] P9：Linux 行为基线与独立 Qt 候选评估完成，保留 efsw；三平台、网络路径、资源限制与集成验证待完成。
- [ ] P10：静态审计与显式 TLS 诊断入口完成；保留 OpenSSL，实际 Windows 产物及应用下载路径待验收。
- [ ] P11：内核、组件、完整后台任务及高亮／本机 Vectorscan 路由验证完成，生产保留；真实日志及其他平台准入待验证。
- [ ] P12–P14：性能库对照实验。
- [ ] P15–P16：仅在证据支持时开展架构级实验。

**第一批结果：** 见[第一批依赖清理计划](2026-11-13-dependency-cleanup-batch1.md)。

| 项目 | 提交 | 验证与结果 |
| --- | --- | --- |
| 前置修正 | `eaec438` | 将依赖布局检查固定提交同步到 ZzPureTools `d8afb260`；修复初次基线中的 1 项失败 |
| P0 构建基线 | `0faa93a` | Linux Release、Qt 6.11.1、GCC 15.2.0、LTO ON；`ci_build` 成功，CTest 92/92；已有 `-Wodr` 警告单独保留记录 |
| P1 移除 whereami | `ca5d20d` | 新目录配置和完整构建成功，CTest 92/92；删除 26 个依赖文件、75,348 字节及构建／NOTICE 条目 |
| P2 已核实部分 | `75fdec6` | 删除 12,989,440 字节旧签名客户端及失效配置，补齐当前依赖目录和 KDE 下载依赖记录；19 项相关 CTest 通过，Python CI 124 项、3 项跳过；有效打包步骤保持一致 |

两项删除合计约 12.46 MiB 的受跟踪文件，不含其他文本变化；未测量安装包或性能收益，也未缩减 Git 历史。Windows/macOS 原生验证未执行。

**下一项：** 继续 P11，完整后台任务的可选实验已验证查询直接回退、字节限额及实际取消／进度／更新；高亮偏移和本机 Vectorscan 路由已验证，外部日志清单／固定快照入口及本地流程验收也已完成；接下来用代表性业务日志复测并补充其他平台证据，再决定生产准入；StringZilla 已完成本轮对照，暂不采用。P10 已提供诊断入口，删库仍需实际 Windows 产物与应用下载路径验收。P9 的三平台、网络路径、资源限制及完整集成待验证，条件充分前保留 efsw。P3 已关闭并保留 maddy；P2 剩余的全量依赖来源、许可证和本地补丁审计继续保留为待办。


### KDE 依赖升级与源码纳入仓库（2026-11-13）

按用户指定，将 KSyntaxHighlighting 和 Extra CMake Modules 从 6.22.0 升级到 `v6.31.0`，完整官方发布源码分别纳入 `3rdparty/vendor/syntax-highlighting/` 与 `3rdparty/vendor/extra-cmake-modules/`，不再使用 FetchContent 下载。两份源码包已对照官方 SHA-256 校验；3,044 个上游文件与原包逐字节一致，额外添加两份来源说明。源码约 47.7 MiB，这是本次选择离线源码分发的体积成本。

宿主要求同步为 CMake 3.30+、Qt 6.10+，保留静态链接与内嵌语法/主题。CI 增加共享 CMake 版本检查，构建文档、依赖清单和发布版权说明同步更新。旧的 `FETCHCONTENT_SOURCE_DIR_ZZLOGG_*` 覆盖参数不再生效。

验证环境：Linux x86_64、GCC 15.2.0、Qt 6.11.1、CMake 4.3.3，Release、LTO OFF、WARNINGS_AS_ERRORS ON，标准 Qt 正则后端，单元/集成/UI 测试 ON。全新 `out/build/kde631-vendor` 在 `FETCHCONTENT_FULLY_DISCONNECTED=ON` 且 HTTP/HTTPS/ALL_PROXY 指向不可用本地端口的情况下配置成功；`ci_build` 通过，CTest 92/92，Python CI 124 项、3 项跳过。旧的 6.22.0 构建目录也已重新配置，构建图改用 vendor 中的 6.31.0 源码。缺失 vendor 源码时立即给出本地路径错误，不回退联网下载。

Windows/macOS 原生构建尚未运行；这两项依赖离线不代表 Qt SDK、子模块初始化及其他 CI 工具安装均无需网络。


### P3 直接替换兼容性评估（2026-11-13）

用户要求：不接受以功能勉强可用代替现有体验。评估基线为 `873384c`，Linux、Qt 6.11.1、offscreen 平台。现有应用测试 `documentationFollowsApplicationLanguage` 通过；对照程序使用同样的三语 Markdown、maddy 生成的 HTML、752×720 窗口、11pt 字体、24px 文档边距和现有 CSS，分别调用 `setHtml()` 与 `setMarkdown()`。这里只评估直接替换，不声称 Qt 无法通过额外适配实现等效。

| 项目 | 现有 maddy → QTextBrowser HTML | Qt Markdown 直接加载 |
| --- | --- | --- |
| 三语目录锚点 | 每种语言检测到 22 个锚点 | 每种语言均为 0 |
| 跳转 `command-line-options` | 简中／英文／繁中滚动位置分别为 8520／8435／8578 | 均停留在 0，跳转失败 |
| 表格与链接数量 | 每种语言 7 个表格、21 个不同链接目标 | 数量一致，但数量不能证明导航或样式等价 |
| 深色模式链接 | `#75bfff`，与原样式一致 | `#0000ff`，未采用现有 CSS 配色，可读性下降 |
| 段落和表格样式 | 原有间距、表头底色、单元格内边距 | 间距改变；表格改为默认边框/内边距，表头样式未保留 |
| 命令行字面量 | `--data-dir <path>` 正常显示 | 该字面量仍正常显示 |

对照截图、导出的文档 HTML、探针源码和原始输出保存在本机忽略目录 `out/benchmarks/p3-probe/`；应用现状截图在 `out/benchmarks/p3-baseline/`。这不是 Windows/macOS 原生验证，也没有完成替代渲染器的实现。

最终决定：用户明确要求不再考虑替换 P3，因此保留 maddy，关闭此项，不改变应用或打包行为。后续直接推进 P4；上面的兼容性分析不构成待执行的替换计划。

### P4 信号节流依赖精简（2026-11-13）

采用基于 QTimer 的 `SignalThrottler`，迁移文件监控和搜索进度两个调用点。保留 250 ms／100 ms、默认 CoarseTimer、持续事件定期通知、回调重入以及析构时同步发送待发通知的原有行为；未引入未使用的 leading/debounce 或 QML 接口。P3 已由 `5fcff8c` 关闭，maddy 保留。

同一组行为测试先在 KDSignalThrottler 上通过；空的新实现按预期无法通过通知和析构用例，补齐后通过。删除 KDToolBox 的 286 个受跟踪文件，共 895,680 字节（约 0.85 MiB），移除其构建目标与链接，保留改编代码及 KDSingleApplication 的 MIT 归属声明。新增组件的头文件与实现共 73 行；这表示维护范围收缩，未测量安装包或运行性能收益。

Linux x86_64、GCC 15.2.0、Qt 6.11.1、CMake 4.3.3，Release、LTO OFF、WARNINGS_AS_ERRORS ON、单元/集成/UI 测试 ON、Hyperscan/VectorScan OFF。全新 `out/build/p4-throttler` 的 `ci_build` 通过；CTest 93/93，Python CI 124 项、3 项跳过；独立代码审查未发现阻碍合入的问题。Windows/macOS 原生验证待 CI。执行步骤与命令见 [P4 记录](2026-11-13-signal-throttler.md)。

### P5 默认 Qt 搜索后端解码优化（2026-11-13）

采用局部改动：零模式直接返回，多模式复用同一行的 QString 解码结果。保留显式字节长度、Unicode 替换语义、大小写、模式顺序及布尔结果。新增边界与随机字节参考对照、独立可复现的性能/分配探针。

Linux／Qt 6.11.1 下 `ci_build`、93 项 CTest、Python CI 124 项（3 项跳过）通过。ABBA 热扫描对照中，中文多模式约 1.8–3.9 倍，非空 4／16 模式分别减少每行 3／15 次 glibc 分配请求；零／单模式分配不增加。相同源码控制仍存在明显 ASCII 计时偏差，因此单模式及 ASCII 性能无法可靠判定，不声称所有查询提速或绝无退化，也不将局部收益解释为整应用倍速。Windows/macOS 原生验证待 CI。完整配置、原始数据位置、结果及限制见 [P5 记录](2026-11-13-regex-decode-once.md)。

### P6 布尔求值器迁移（2026-11-14）

保留旧实现已接受的公开语法、优先级、结合性、真值表和反选，修复旧 ExprTk 对部分复合 `nand/nor/xor/xnor` 查询的拒绝。新增明确长度、模式数和嵌套上限，诊断不再使用 ExprTk 内部错误码。删除 ExprTk、robin-hood 的目标、源码、失效模块及许可证清单项，清理 vendor 源码约 7.22 MiB。

全新 Linux 构建、93 项 CTest、Python CI 124 项（3 项跳过）通过，独立复审无阻塞问题。布尔组件创建成本明显下降，64 变量链的热求值有收益，部分小表达式基本持平；不承诺所有查询或整应用提速。Windows／macOS 待 CI。语法差异、边界、测量及复现方法见 [P6 记录](2026-11-13-boolean-evaluator.md)。

### P7 构建依赖与后端配置（2026-11-14）

工具库停止传播无用的 Network／Concurrent；regex、logdata、filewatch、support 的实现依赖收为 PRIVATE，UI 补齐真实使用依赖。核心继续 C++17，框架／UI 保留 C++20，修正过时的 C++14 声明。裸 CMake 与基础预设默认 Qt；新增显式加速预设，CI 保留 Windows Hyperscan／macOS Vectorscan，冲突开关报错。

标准 Linux `ci_build`、96 项 CTest、Python CI 124 项（3 项跳过）通过；全新 Vectorscan 构建通过真实单／多模式 matcher 及回退测试。Windows／macOS 原生验证待 CI。不将依赖边界整理解释为运行性能提升。完整审计、验证范围与复现方法见 [P7 记录](2026-11-14-build-dependencies.md)。

### P8 强类型依赖评估（2026-11-14）

保留 type_safe，补充五种单位的编译期隔离、数值边界、Qt 元类型、调试输出与哨兵回归。现有库约 446 KiB，无独立运行模块；本机各类型与底层整数等大，简单运算可内联。局部结构的差值计算少两条指令，但没有实际运行性能收益证据。Release 的窄化前置条件也不能直接用 static_cast 取代。

本轮不改变生产类型与转换行为。Linux 完整构建、96 项 CTest 及 Python CI 124 项（3 项跳过）通过；新增 107 条运行时边界断言和编译期单位隔离检查。方法、已知边界和验证范围见 [P8 记录](2026-11-14-strong-types.md)。


### P9 文件监控基线与候选评估（2026-11-14）

新增生产 FileWatcher／LogData 回归和独立 Qt 候选探针，记录同元数据覆盖的轮询盲区及父目录重建恢复差异。候选不进入应用或默认 CI 构建，生产保留 efsw。Linux 完整构建、97 项 CTest 及 Python CI 124 项（3 项跳过）通过；一次 ABBA 采样不足以证明候选整体性能更好。

P9 尚未完成三平台、真实网络路径、资源限制与生产集成准入；不得视为已经替换依赖。行为矩阵、测量限制与复现方法见 [P9 评估记录](2026-11-14-filewatch-evaluation.md)。


### P10 Windows TLS 静态审计与诊断入口（2026-11-16）

明确安装包额外复制旧 OpenSSL DLL、便携包使用统一 runtime 的差异，并记录现有 Schannel 插件契约。新增显式 `zzlogg_tls_probe`，支持真实后端与 Windows 加载模块报告、HTTPS 内容校验、证书失败、重定向、代理和超时；默认 CI／应用不包含该工具。Linux 14 项本地真实 TLS／CONNECT 测试、完整构建、97 项 CTest 及 Python CI 124 项（3 项跳过）通过。

本机使用 Qt 6.11.1 的 OpenSSL 后端，不能据此推断 Windows 的依赖；未验证 Windows 编译、系统证书／代理或最终产物。保留当前 OpenSSL 打包，不标记整个 P10 完成。详细边界和 Windows 复现入口见 [P10 记录](2026-11-16-windows-tls-audit.md)。


### P11 纯文本与多关键词首轮对照（2026-11-16）

独立比较真实 Qt 默认匹配器、QString、标准库、Rust memchr／aho-corasick；完整位图及异常输入回退契约通过，147 场景完成 4 轮配对测量。标准库和自动机各存在明确压力场景退化，生产暂不接入候选。Rust 可选，正常构建无需新增工具链。

无 Rust／有 Rust 两种独立构建、97 项 CTest 及 Python CI 124 项（3 项跳过）通过。待补 C/C++ 候选、真实日志、内存、应用集成与跨平台验证；测量包含批量接口及 checksum 开销，不是整应用倍数。详见 [P11 首轮记录](2026-11-16-literal-search-evaluation.md)。


### P11 StringZilla 补充对照（2026-11-16）

独立实验增加固定版本的 StringZilla Haswell／串行入口；不增加应用依赖或自动下载。完成 147 场景、4 轮 Qt 及 memchr 直接配对，共 14,112 条热扫描记录。Haswell 在本轮合格输入中快于 Qt，但相对 memchr 的倍率为 0.209～0.994，未显示更快的场景；memchr 同实现控制为 0.992～1.015。StringZilla 构造较轻、无需 Rust 工具链，仍不足以据此直接接入生产。

三种独立构建、边界／保护页契约、项目构建、97 项 CTest 及 Python CI 124 项（3 项跳过）通过。当前决定是保留生产实现，继续实际管线和平台准入；未完成整个 P11。数据、源码成本、体积对照和测量限制见 [StringZilla 记录](2026-11-16-stringzilla-evaluation.md)。


### P11 生产组件对照（2026-11-18）

独立可选探针复用生产 RawLines 转码／分行、BooleanExpressionEvaluator 和 Roaring 行号收集，以真实 PatternMatcher 为 Qt 参考。32 场景、四轮、2048 条热记录及 128 次独立进程诊断完成；逐行及 128／4096 行 memchr 在本轮合格场景仍有明显收益，回退场景无稳定收益，部分更慢。大批次未显示明显额外吞吐收益，却增大临时位图和未检查取消的处理量。

因此生产继续保留原实现，下一步对合格查询做完整后台任务实验；128 行仅作为候选起点，尚需字节限额和长行验证。项目构建、97 项 CTest、Python CI 124 项（3 项跳过）通过；默认构建不查找 Cargo。取消是单线程边界模型，RSS 是整个新进程高水位，不宣称整应用集成完成。具体数据、控制波动及限制见 [组件评估记录](2026-11-18-search-pipeline-evaluation.md)。


### P11 完整后台任务对照（2026-11-18）

独立实验副本使用实际文件索引、PatternMatcher 和 LogFilteredDataWorker/TBB，候选每批最多 128 行／256 KiB；不合格查询直接 Qt，超长行逐行 Qt。22 场景、四轮、704 条计时及 176 条诊断完成，完整结果一致，取消后所有块释放且可重搜，文件尾行更新与旧匹配移除通过。

短行单关键词约快 1.18～1.41 倍，多关键词收益更大；回退无稳定收益，超长行回退本轮耗时增加约 4%～5%。生产保留原搜索后端，正常构建不引入 Rust。默认代码修复取消块泄漏及无换行尾行的虚拟字节／多字节解码问题。ci_build、97 项 CTest、Python CI 124 项（3 项跳过）通过。高亮、加速后端、真实日志和跨平台准入仍待验证；见 [worker 评估记录](2026-11-18-search-worker-evaluation.md)。


### P11 高亮与加速路由验证（2026-11-18）

复用真实 Highlighter／QuickFindPattern 与视图 tab 转换，9 组实际文件验证 UTF-16 列、中文／emoji、NUL／BOM／坏字节、捕获组和大小写回退。默认 3 项／31 条坐标断言通过；6 类查询在 Qt／加速配置及 1／2 匹配线程下核对开关前后完整结果。

Linux x86_64 Vectorscan 构建实际执行单／多模式加速 matcher，worker 类型诊断确认选择加速引擎时不创建 memchr；Qt-only 构建的强制加速验收会正确失败。默认构建、97 项 CTest、Python CI 124 项（3 项跳过）通过。没有新增性能结论或生产依赖；真实日志、独立 Hyperscan、其他平台和完整 UI 渲染仍有验收边界，见 [记录](2026-11-18-search-highlight-routing.md)。


### P11 外部语料入口（2026-11-18）

现有 worker 探针支持 JSON 日志／查询清单，runner 用固定只读副本完成跨轮对照，记录内容／完整配置／结果摘要及构建信息。默认应用无变更。两份已结束的本机构建和 CTest 日志完成 8 场景、四轮、256 条计时与 64 条诊断，完整结果一致；小文件不足以证明业务负载和有效早期取消表现。

默认构建、97 项 CTest、Python CI 131 项（3 项跳过）、真实文件／错误／超时／输出链接保护及既有实验契约通过。用户代表性语料、其他平台及生产准入仍待完成，详见 [语料入口与证据](2026-11-18-search-corpus-input.md)。
