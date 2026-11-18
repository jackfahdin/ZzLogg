# 搜索生产组件对照探针

P11 的下一层实验：Qt 参考调用真实 `RegularExpression`／`PatternMatcher`；双方均调用生产 `RawLines::buildUtf8View()`、`getUntabifiedLength()` 和 Roaring 结果容器。memchr 候选复用前轮 `LiteralMatcher`，布尔结果由真实 `BooleanExpressionEvaluator` 求值。候选并未注入生产 PatternMatcher 或后台任务。

## 构建与运行

在已有可构建应用的 Qt 环境中，显式启用实验；默认 OFF，不进入 all、ci_build 或安装包。Cargo／rustc 只在显式启用时需要。复用前轮锁定的 memchr 2.8.3／aho-corasick 1.1.5（后者只属于共享实验 crate，不是本轮候选），许可证均为 MIT／Unlicense；这里不增加新的 Rust 来源或应用依赖。

```bash
cargo fetch --locked --manifest-path tools/benchmarks/literal_search/rust/Cargo.toml
cmake -S . -B out/build/p6-boolean -DZZLOGG_BUILD_SEARCH_PIPELINE_PROBE=ON
cmake --build out/build/p6-boolean --target zzlogg_search_pipeline_probe --parallel 8
out/build/p6-boolean/output/zzlogg_search_pipeline_probe --verify
python3 scripts/dev/benchmark_search_pipeline.py \
  --binary out/build/p6-boolean/output/zzlogg_search_pipeline_probe \
  --output out/benchmarks/p11-pipeline/results --rounds 4
```

首条命令是显式获取依赖；CMake 使用 `--locked --offline`。Windows 可执行文件带 `.exe`，输出路径取决于生成器；本轮只验证 Linux。实验继承主工程编译选项，复现时应检查 CMakeCache 和编译命令，包括 CPU 指令集和 LTO，不能直接与前轮独立工程绝对耗时横比。

后端为 `qt`、`memchr-line`、`memchr-128`、`memchr-4096`。后面三者分别以 1／128／4096 行调用前轮通用批次适配，每个批次只跨一次 Rust FFI；逐行版本也承担通用适配的向量／位图分配，它不是已经优化好的最小逐行 FFI。Qt 对照始终调用生产逐行接口，探针为它每 4096 行设置一个取消检查边界。

## 测量与语义

32 个确定性合成场景：4 类内容／编码（ASCII、Unicode UTF-8、UTF-16LE、Latin-1）× 2 种行长度（128／4096 UTF-8 字节）× 3 种模式数（1／16／64），另有非法 UTF-8、忽略大小写、正则、空模式回退、预过滤、长公共前缀、高重叠和较大短行块。常规短行 4096 行，长行 512 行，`large-short` 为 8193 行。编码后的原始字节长度可能不同。普通场景每隔一行注入一个关键词，多模式使用 OR；八种运算符、反选、引号及重复模式由契约覆盖，不声称已测任意布尔组合的性能。

候选仅对全部非空、区分大小写、有效 UTF-16 的纯文本模式启用 memchr；异常 UTF-8 和行首 BOM 回退 Qt。整个受控查询的公开字符串及 ID 布尔式由相同模式集合生成；真实解析器用于 Qt 校验，候选不复制解析器，且不对外提供任意查询解析接口。正则／忽略大小写／空模式回退也要逐项对照完整行结果。

计时从块转换开始，包含转换／分行、编码验证、匹配、布尔求值、最大字节宽度（含制表符展开）和行号收集。输入构造与编码不计时；没有磁盘读取、索引、TBB 图调度、多工作线程、UI／高亮。双方各有独立 decoder 和转码缓冲，首轮前均未运行转换。热扫描复用相同缓冲容量；只测上述无状态编码，不外推到有状态编码流。

`processed` 是探针实际访问的转换后视图数；生产 worker 的进度字段使用原始索引行数，二者在删换行预过滤时可能不同，因此不能据此声称完整 worker 进度语义已经验证。完整行号、最大宽度、实际处理量和转换视图数组成 SHA-256；runner 核对每个后端／每轮的输入和完整结果，任意不一致即失败。

每次配对双方先记录构造和首次扫描，再预热，目标 10 ms、1～32 遍、ABBA 热扫描。默认四轮，每侧每场景八个热样本；构造／首次各四次独立测量（两个热记录重复携带同一次构造／首次数据）。汇总为两侧热样本中位数之比。构造包含 RegularExpression 验证，首批包含首次转码与 Qt JIT；都不是冷进程启动时间。`qt` 候选为同实现控制。Linux 固定 CPU、不锁频；不要并发编译或跑其他负载。跨场景中位数不是整应用加速倍数。

热计时只加总匹配数量、最大宽度与处理量以消费结果，不做 SHA 或逐批时钟采样。完整摘要在首次扫描计时外校验。原始 JSONL、stderr、源码／二进制摘要、构建缓存、CPU 与 Rust 版本保存在输出目录。

## 内存与取消边界诊断

runner 在热测量结束后，为每个场景／后端启动独立进程执行 `--profile --case <名称>`，先完整运行一次，再采集转换时间、最长批次服务时间和最大子模式位图有效字节数。`peak_process_rss_kib` 是该新进程的 Linux 高水位，包含原始样本、转码、Qt／Rust 运行时和诊断结果；非 Linux 为 -1。它既不是 memchr 的净占用，也不是精确分配次数。位图有效字节数不包含 capacity、ByteView 数组和容器其他开销。

取消模型是单线程、确定性的：预先取消不转换；在首批刚通过边界检查后注入请求，当前批次匹配／求值／收集完成后停止。记录已处理行数与该批次耗时。它没有真实后台取消线程，当前实际 worker 也没有探针的批内检查；这些数据仅揭示一个不可中断批次的服务成本，不是应用取消延迟保证，也不是最坏时间上界。转换本身仍不可中断；长单行也不能靠缩小行数批次解决。

契约覆盖固定行号与宽度、完整 Qt 对照、八种运算符、反选、重复／重叠、引号、Unicode 大小写、正则和空模式回退、UTF-8／UTF-16LE／Latin-1、BOM／NUL／坏字节、CRLF／无尾换行、预过滤改变行数、空输入、尾批、预取消和重复扫描。

结果与待完成准入见 [本轮记录](../../../docs/superpowers/plans/2026-11-18-search-pipeline-evaluation.md)。不把这个组件实验标为整个 P11 或整应用集成完成。
