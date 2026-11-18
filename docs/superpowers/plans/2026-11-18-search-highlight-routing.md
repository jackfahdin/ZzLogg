# P11：高亮坐标与加速后端路由验证

> 使用 writing-plans / executing-plans 延续已授权路线；本地 master 中文提交，不推送。

**目标：** 验证候选命中行能够交给现有 Qt 高亮，UTF-16 字符坐标及制表符展开正确；显式选择既有加速引擎时实验 memchr 不介入。

**规格：** [完整后台任务记录的下一步](2026-11-18-search-worker-evaluation.md)。生产保持现有依赖和默认引擎，不能为了替换牺牲语义。

## 方案和边界

后台 PatternMatcher 仅返回行是否命中，不返回位置。显示层 Highlighter／QuickFindPattern 用 Qt 计算 UTF-16 偏移；AbstractLogView 再展开 tab。布尔／反选／空查询的主搜索高亮已有独立禁用规则，不改变该规则。采用真实 worker 加真实高亮组件的隔离契约，避免实现第二套匹配位置算法；不把原始 UTF-8 字节偏移直接用于显示。

将视图已有的线性 tab 坐标转换原样提取为 HighlightedMatchRanges::expandTabs(QString)，视图与契约调用同一实现。只编译两个必要 UI 源文件进现有实验 executable，使用实验 regex，避免链接整个正常 klogg_ui 导致两版 regex ABI 混用。默认 UI 测试也覆盖坐标转换和真实 Highlighter。

加速构建复用仓库已有 Vectorscan 配置，必须断言实际 HsSingleMatcher／HsMultiMatcher 被创建；只设置配置枚举不算加速验证。候选启用与关闭都使用选定引擎，逐项比较完整后台结果，含布尔／反选／Qt 语法回退。默认构建另外验证没有加速库时的兼容路径。无新性能测量，前轮数据保持历史快照。

本轮不作真实用户日志、Windows／macOS／ARM 或截图／像素渲染验证，不把 Vectorscan 证据冒充独立 Hyperscan 构建验证。

## 文件

- `src/ui/include/highlightedmatch.h`、`src/ui/src/abstractlogview.cpp`：提取并复用 tab 转换。
- `tests/ui/searchhighlight_test.cpp`、`tests/ui/CMakeLists.txt`：默认高亮坐标契约。
- `tools/benchmarks/search_worker/{integration.cpp,harness.h,main.cpp,CMakeLists.txt,README.md}`：显式 `--verify-integration`，可加 `--require-accelerated` 禁止无库假通过。
- 本文和路线图：证据、边界、下一步。

## 审查重点

- 中文、代理对、NUL、坏字节不能将 UTF-8 字节索引当成 UTF-16 列；tab 前后坐标与颜色需保持。
- 正则捕获组／多次匹配、字面正则元字符、大小写回退使用现有 Qt 语义。
- 加速编译与引擎选择是两个维度；加速配置必须真实执行库，memchr 不可覆盖用户选择。
- 新 UI 依赖只属于实验，不能链接原版 regex/logdata 或污染默认构建。
- 测试用手工预期位置与完整行结果；既有加速差异不能被候选开关掩盖。

## Task 1: 高亮与路由契约

- [x] 添加默认坐标测试，以尚未实现的 expandTabs 空实现观察 UTF-16／tab 预期失败，再迁移现有视图转换实现。
- [x] 在独立副本中串起真实文件搜索、显示解码、Highlighter 和 QuickFindPattern：UTF-8／UTF-16LE／Latin-1、BOM／NUL／坏字节、重复字面匹配和正则捕获；固定命中行及原始／展开坐标。
- [x] 添加默认／加速路由契约及严格要求加速库的命令；在 Qt-only 和 Vectorscan 两种构建验证，后者必须实际创建加速 matcher。
- [x] 默认 ci_build、完整 CTest、Python CI、原 worker 契约；加速构建相关测试和新契约，检查链接隔离。
- [x] 独立只读审查、文档更新、中文提交，不推送。

Expected: 坐标与固定预期一致；候选开关不改变引擎选择；加速路径有实际库证据；正常应用仍无 Rust。

## 2026-11-18 验证结果

**默认高亮算法保持不变，候选与现有高亮组件的接口契约通过；Linux x86_64 的 Vectorscan 路由得到实际执行证据。** 本轮不替换生产后端，也没有新的性能结论。

### 坐标与显示接口

默认高亮测试 3 项、31 条断言通过。先以空的 tab 转换实现得到 3 项失败，再把视图已有单次前向扫描原样迁移，观察全部通过；没有重写匹配位置算法。测试检查范围内含 tab、可视裁剪、颜色、Unicode 和捕获组。

以 `中😀\tERROR error ERROR` 为例，两个大写匹配的原始 UTF-16 起点为 4、16，tab 展开后为 8、20，长度都是 5。中文和 emoji 的 UTF-8 字节数不同，不能从后台字节宽度直接推导显示列。正则 `(ERROR) (42)` 则保留两个捕获组的独立位置和长度。

独立工具在 9 组实际文件中执行后台搜索、获取显示文本、调用生产 Highlighter／QuickFindPattern：UTF-8、UTF-16LE、Latin-1，CRLF／尾行、中文／emoji、NUL／坏字节／BOM，字面元字符、大小写回退及捕获组。每组核对 Qt／候选完整搜索摘要、固定命中行、原始／展开位置；纯文本同时核对快速查找的正向／反向定位。坏字节验证使用 Qt 显示解码，不外推为加速库对无效 UTF-8 的支持。

主搜索高亮仍沿用原规则：布尔、反选和空模式不创建普通高亮器。本轮没有修改也没有用截图验证该绘制策略。自定义高亮集合的优先级、主题和像素渲染并非本轮新增验收范围。

### 加速后端路由

六类查询为纯文本、嵌套布尔、反选、忽略大小写、带 lookahead 的正则和空模式；在 Qt／Hyperscan 配置枚举下各用 1／2 个匹配线程，比较实验开关前后的完整结果，并核对固定行号及处理位置。

Vectorscan 构建不只检查编译宏：直接创建并执行 HsSingleMatcher／HsMultiMatcher，实际 worker 中再统计最终选择的 matcher 类型（含加速预过滤器）。在选定加速引擎、非空纯文本的场景，计数必须等于匹配线程数，而 memchr matcher／批次必须为零；选 Qt 时加速计数必须为零。诊断代码仅在实验副本编译。先以缺少类型诊断观察断言失败，再补实际类型记录并通过。

Qt-only 构建中的加速配置枚举保留原 Qt 兼容路径；`--verify-integration --require-accelerated` 必须非零退出，防止误将无加速库的运行记为加速验收。该参数脱离 `--verify-integration` 使用也被拒绝。

这里验证的是仓库 vendored Vectorscan 在当前 Linux x86_64 环境中的路由与有限输入结果，不代表所有正则／坏编码与 Qt 等价。独立 Hyperscan 构建、Windows／macOS／ARM、真实用户日志和冷磁盘仍待验证。

### 构建、回归及边界

- 默认 `ci_build` 成功，CTest **97/97**；Python CI **124 项、3 项跳过、无失败**。
- Qt-only／Vectorscan 的新集成契约及既有完整 worker 契约均通过；Vectorscan 的原后端契约 **2 项、20 条断言**通过。
- 独立只读审查未发现 Critical／Important 问题，核对原算法等价性、坐标测试、实际 matcher 证据和两份链接图。审查提到的 CTest 失败索引是 11 月 14 日旧文件；本轮完整 stdout 中该配置测试通过，97 项最终汇总为零失败。
- 两种构建的 all／ci_build 命令图不含实验或 Rust；探针链接实验版 regex/logdata，未链接正常 klogg_ui／regex／logdata。额外 Qt Widgets 和高亮源文件只属于显式探针。
- 隔离编译发现 highlighterset.cpp 间接包含编辑器生成 UI 头，本轮移除该无用依赖并直接包含实际需要的 QApplication。正常应用与独立副本均成功构建；该整理不改变高亮行为。
- 环境沿用 Linux x86_64、Qt 6.11.1、GCC 15.2、Release、LTO OFF，Vectorscan 使用项目既有构建选项。原始命令与工具边界见 [README](../../../tools/benchmarks/search_worker/README.md#高亮坐标和既有加速后端)。
- 本轮改动工具链接组件，后续重新测量必须记录新的源码／二进制摘要；runner 已把 UI 源码纳入摘要。前轮性能结果对应原二进制，保持历史快照。

下一步准备可复现的真实日志语料输入与对照报告，再补其他平台和工具链分发成本。真实语料的代表性、隐私处理和平台结果未确认前，P11 不进入生产采用阶段。
