# 实际后台搜索任务对照

P11 的显式实验：使用真实文件索引、PatternMatcher、LogFilteredDataWorker、TBB、进度和结果收集，测试 memchr 候选。只在独立副本中定义 `ZZLOGG_EXPERIMENTAL_SEARCH`，默认应用仍使用原有搜索后端，不增加 Rust 依赖。正常源码中的取消块所有权和虚拟 EOF 读取修复同时适用于默认应用。

## 构建与运行

```bash
cargo fetch --locked --manifest-path tools/benchmarks/literal_search/rust/Cargo.toml
cmake -S . -B out/build/p6-boolean -DZZLOGG_BUILD_SEARCH_WORKER_PROBE=ON
cmake --build out/build/p6-boolean --target zzlogg_search_worker_probe --parallel 4
out/build/p6-boolean/output/zzlogg_search_worker_probe --verify
python3 scripts/dev/benchmark_search_worker.py \
  --binary out/build/p6-boolean/output/zzlogg_search_worker_probe \
  --output out/benchmarks/p11-worker/final --rounds 4
```

需要已有的主工程 Qt 构建环境。首条命令显式获取既有锁定 Rust crate，CMake 使用 `--locked --offline`；复用 memchr 2.8.3，共享 crate 中的 aho-corasick 1.1.5 不是本轮候选，均为 MIT／Unlicense。不开实验时不查找 Cargo，目标不进入 all、ci_build 或安装包。两个主工程探针共用 `literal_search/cmake/ProbeRust.cmake`，可分别启用。

直接运行默认后端为 `qt`；显式 `--candidate memchr` 选择候选，`--case short1-t1` 选单场景，`--reverse` 颠倒配对顺序，`--profile` 收集独立诊断。runner 默认依次测试两种候选，Linux 固定前两个可用 CPU，也可用 `--cpus` 指定；运行期间不要编译或施加其他重负载。Windows 可执行文件扩展名及生成器输出路径不同；本轮只验证 Linux。

## 路由与边界

候选直接使用生产解析后的子模式顺序和布尔求值器，不复制查询解析。仅 Qt 配置、全部非空、区分大小写、有效 UTF-16 的纯文本子模式进入候选。空模式、正则或忽略大小写查询直接走原有 Qt；批次内的坏 UTF-8／行首 BOM 仍按既有实验适配回退 Qt。支持反选及任意合法布尔结构，但有限契约不能证明所有输入。

每批最多 128 行、256 KiB UTF-8 数据；单行超限直接调用 Qt。此限额不限制原始文件块读取／整块转码内存，也不是取消延迟上界。转码前和批次间检查取消；已经进入的转码和单次 Qt 调用不可中断，取消后的不完整块不发布。原有 Qt 路径仍以文件块为主要取消边界。

所有权使用 shared_ptr，保证队列、活动块、取消出口及未入队块释放。只在实验副本加入生命周期计数；Qt 与候选都承担相同计数开销。正常应用没有这些诊断计数。其他候选计数在计时时关闭。

## 测量内容

22 个合成场景：11 种数据／查询 × 1／2 个匹配线程。`short1`／`short16` 为 8192×128 字节、1／16 模式；`long16` 和 `utf16` 为 2048×4096 UTF-8 字节、16 模式；`mixed` 每十行一个坏字节；`boolean` 为零命中的嵌套布尔查询（不代表有命中时的结果合并成本）；另有大小写、正则、空模式、预过滤和 `oversized`（32 个超出 256 KiB 的行）。文件读取块为 1024 个索引行。中文 UTF-16 编码后的原始文件长度不同。

先实际索引临时文件，再生成 Qt 完整结果参考并预热两方。每次计时从提交新搜索开始到获取结果、销毁 worker／等待后台任务释放结束，包含表达式与匹配器构建、文件读取、转码、TBB 调度、匹配／结果合并和进度投递。worker 对象本身在计时前创建；文件生成／索引、结果 SHA 在计时外。已预热文件使用页缓存，不代表冷磁盘或应用首次打开文件。

每次测量都有新的搜索任务与匹配器。四轮交换进程顺序及 ABBA／BAAB 顺序，每侧每场景八个计时样本，合计 704 条记录。Qt／Qt 同实现控制用于观察波动。每一次完整结果的行号、移除行号、最大宽度及处理位置都核对 SHA-256，跨轮核对输入签名。中位数之比仅描述当前场景，跨场景分布不是整应用倍数；不设速度 CI 阈值。

计时之后每轮分别启动两种候选的诊断进程，合计 176 条场景诊断。报告实际批次数、批次行数／字节上限、超长行 Qt 回退数量、活跃块及取消请求到任务释放的耗时。`batched_rows` 包括进入适配后又回退 Qt 的坏编码行，不能当作全部 Rust 快路径行数。取消由主事件循环收到首个中间进度后发出，包含真实线程排队；小任务可能在请求前已全部发布结果。原有取消操作仍发送终止 100，不能据此判断全量搜索完成。`cancel_processed` 是原有处理位置，不是保证连续已处理行数。

契约覆盖固定命中、复杂布尔／反选、回退、三种显示编码、BOM／NUL／坏字节、范围、空文件、预过滤、批次字节／行数边界、超长行、预取消、同步／异步取消后重搜、同一 worker 的尾行追加及旧匹配移除。生命周期计数必须归零；默认应用另有取消和多编码 EOF 回归。

原始 JSONL、stderr、源码／二进制 SHA、CMakeCache、CPU 与 Rust 版本保存在输出目录。结果及未完成准入见 [本轮记录](../../../docs/superpowers/plans/2026-11-18-search-worker-evaluation.md)。真实用户日志、高亮、加速后端及其他平台仍需验证，生产采用未获准入。

## 高亮坐标和既有加速后端

```bash
out/build/p6-boolean/output/zzlogg_search_worker_probe --verify-integration
# 在已配置 KLOGG_USE_VECTORSCAN=ON 的构建目录中显式启用本探针后：
cmake -S . -B out/build/linux-gcc-vectorscan-release -DZZLOGG_BUILD_SEARCH_WORKER_PROBE=ON
cmake --build out/build/linux-gcc-vectorscan-release --target zzlogg_search_worker_probe --parallel 4
out/build/linux-gcc-vectorscan-release/output/zzlogg_search_worker_probe \
  --verify-integration --require-accelerated
```

`--require-accelerated` 在没有编译加速库时失败，不把设置引擎枚举当作验证。集成契约执行实际加速 single/multi matcher，并在实际 worker 中记录所选 matcher 类型：选择加速引擎时不得创建 memchr 批次；选择 Qt 时不得采用加速 matcher。没有库的构建则验证保留的 Qt 兼容路径。

高亮仍由 Qt 对显示 QString 计算 UTF-16 坐标。探针只额外编译真实 Highlighter／QuickFindPattern 源文件，复用视图的 tab 坐标展开，未链接整个 klogg_ui；9 组实际文件检查覆盖编码、中文／emoji、NUL／坏字节／BOM、字面元字符、大小写回退和捕获组。加速路由的六种查询使用有效 UTF-8，不声称坏编码上的加速语义与 Qt 全部一致。没有截图／像素测试，也没有改动布尔／反选的主搜索高亮策略。

增加高亮组件后探针会链接 Qt Widgets；这只影响显式工具。前轮性能结果对应当时的二进制，不将新工具体积／运行开销倒算进旧数据。本轮仅做正确性验证，详见 [高亮与路由记录](../../../docs/superpowers/plans/2026-11-18-search-highlight-routing.md)。

## 外部日志语料

复制 [清单示例](corpus.example.json)，将 `application.log` 改为要测试的已停止写入的本地日志，按真实查询填写模式。相对路径从清单所在目录解析，不受运行命令的目录影响。

```bash
python3 scripts/dev/benchmark_search_worker.py \
  --binary out/build/p6-boolean/output/zzlogg_search_worker_probe \
  --corpus /absolute/path/queries.json \
  --output out/benchmarks/my-corpus --rounds 4 --timeout 600

# 单独检查一个清单；直接 probe 每次执行也会生成临时输入副本
out/build/p6-boolean/output/zzlogg_search_worker_probe \
  --corpus /absolute/path/queries.json --candidate memchr

# 显式工具验收，不是性能跑分
python3 scripts/dev/verify_search_corpus.py \
  --binary out/build/p6-boolean/output/zzlogg_search_worker_probe
```

顶层字段仅为 `version: 1` 和非空 `cases`。每条 case 的必填项为 `id`、`file`、`encoding`、`pattern`、`kind`。id 使用 1～64 个 ASCII 字母／数字／点／下划线／连字符，首字符为字母或数字，不可重复。kind 为 `literal`、`regex`、`boolean-literal` 或 `boolean-regex`；后两者使用生产布尔查询语法。编码是 Qt 支持的显式名称，例如 UTF-8、UTF-16LE、ISO-8859-1，不自动猜测。

可选项：`case_sensitive` 默认 true，`inverse` 默认 false，`threads` 默认 1（1～256），`chunk_lines` 默认 1024（1～1000000），`prefilter` 默认空字符串，`timeout_ms` 默认 30000（1～3600000）。数字必须为整数，布尔必须是真正 JSON 布尔值；未知字段／编码、无效查询／预过滤立即失败。`--corpus` 不能与生成样例 `--case` 或 `--verify*` 混用，避免悄悄忽略外部输入。

runner 先以 1 MiB 缓冲流式复制每个不同源文件到私有临时目录，同一文件的多条查询共用副本；复制时发现大小／时间／文件身份变化即拒绝。应提供停止写入的普通文件，这种检查不构成文件系统原子快照。探针再用 1 MiB 缓冲准备自己的索引文件，索引内存和搜索块内存并不受复制缓冲大小限制。所需额外磁盘约为全部语料快照加当前 case 的临时副本；正常退出时自动清理临时文件，runner 在硬超时后也会清理其管理的子进程临时目录；源文件不修改。输出目录不能包含源日志或清单，防止报告文件覆盖输入。

计时仍使用前述热页缓存和完整搜索边界；复制、内容 SHA、索引、完整结果 SHA 在计时外。每条记录新增 `file_sha256` 和 `input_bytes`，runner 与快照核对；`input_sha256` 采用版本化结构，包含文件内容摘要、编码、全部查询标志、预过滤、线程／块大小和软超时。生成样例也改用这套完整签名，不能拿新版签名与旧结果的签名字符串直接比较。

最终 `results.json` 包含原始清单（包括查询与本地路径）、每个源文件内容摘要、所有轮次样本和汇总，不复制日志正文，也不上传数据。所有原始输出和汇总都用新临时文件替换目录项，不沿已有软链接／硬链接写入；完整场景／配对顺序／输入和结果摘要均校验后才发布汇总；安全输出路径检查通过后会先移除旧汇总，后续错误不会留下本轮成功报告。输入与输出冲突时直接拒绝，不删除可能属于输入的旧文件。

`timeout_ms` 是每次索引等待／后台搜索的软超时，不能立即中断正在执行的单次 Qt 调用。runner 的 `--timeout` 是每个 probe 子进程的硬超时（秒），覆盖该进程的全部 case 和诊断，默认 600；大语料可适当调高两者。失败时命令非零退出，检查 stderr 日志，不能仅凭部分 JSONL 判断验收成功。

外部输入和报告方案见 [语料入口记录](../../../docs/superpowers/plans/2026-11-18-search-corpus-input.md)。构建／CI 日志仅可证明流程可用，不能代替用户业务日志的代表性与跨平台性能证据。

## Android 驱动开发模拟语料

无需设备日志即可复现这一组实验。生成器仅用 Python 标准库，固定种子生成 dmesg、threadtime logcat、JSON／堆栈混合应用日志，各 65536 行；另有 12 行异常边界文件。所有内容都是 **synthetic 模拟数据**，不是从设备采集。输出必须是空目录；数据和结果不提交到仓库。

```bash
python3 scripts/dev/generate_android_search_corpus.py generate \
  --output out/benchmarks/p11-android/corpus --rows 65536 --seed 20261119
python3 scripts/dev/benchmark_search_worker.py \
  --binary out/build/p6-boolean/output/zzlogg_search_worker_probe \
  --corpus out/benchmarks/p11-android/corpus/queries.json \
  --output out/benchmarks/p11-android/results --rounds 4
python3 scripts/dev/generate_android_search_corpus.py verify \
  --expected out/benchmarks/p11-android/corpus/expected.json \
  --report out/benchmarks/p11-android/results/results.json
```

34 组查询覆盖驱动 probe 失败、I2C 超时、irq、errno 正则、Java/native 崩溃、binder 排除 debug、包名、中文、ANR、长载荷和反选，各用 1／2 匹配线程。异常文件含 BOM、坏 UTF-8、NUL、CRLF、空行、tab、emoji、无尾换行，以及超过 256 KiB／1 MiB 的行。大行直接走 Qt，其余不符合候选条件的查询仍走原有后端。

生成器独立计算完整命中行号、最大字节宽度和处理位置的 SHA-256，验收全部样本／诊断的结果、文件摘要、配对和两种后端覆盖、路由与批次资源限制。预期不是从探针输出反推；Python 谓词只适用于这组固定模式，不作为通用 Qt 正则参考。`expected.json` 保存生成器源码摘要、参数和每份文件的内容摘要。

这些模拟日志用于可重复的正确性与性能对照，不能代表真实设备的所有格式、分布或大小。单机热页缓存结果不代表冷盘、Windows 或整应用加速倍数；小文件也可能在取消请求到达前已经完成。测量结论见 [Android 语料记录](../../../docs/superpowers/plans/2026-11-19-android-log-corpus.md)。
