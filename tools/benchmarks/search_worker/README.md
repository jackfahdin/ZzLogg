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
