# 纯文本与多关键词实验

这是独立实验工程，不进入应用、安装包或默认 CI，不改变现有搜索后端。默认只需项目已有的 Qt、C++ 和 mimalloc；Rust 和 StringZilla 候选必须显式启用。所有后端返回完整的“行 × 原始模式”位图，保留重复、包含与重叠模式的各自命中。

## 构建与运行

先通过正常项目构建生成 mimalloc 静态库。以下在仓库根目录执行，将 Qt 与 mimalloc 路径替换为本机绝对路径：

```bash
cmake -S tools/benchmarks/literal_search -B out/benchmarks/p11-literal/cpp -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/gcc_64 \
  -DMIMALLOC_LIBRARY=/absolute/path/to/out/build/p6-boolean/output/libmimalloc.a
cmake --build out/benchmarks/p11-literal/cpp --parallel 8
out/benchmarks/p11-literal/cpp/literal_search_benchmark --verify
python3 scripts/dev/benchmark_literal_search.py \
  --binary out/benchmarks/p11-literal/cpp/literal_search_benchmark \
  --candidates std qstring qt
```

最低 Qt 6.10，与主工程一致。仅本机 Linux 构建和执行已验证，Windows／macOS、交叉编译及各平台的 Rust 系统库链接仍需原生验证。工具直接编译当前 `DefaultRegularExpressionMatcher` 生产头文件作为 Qt 基线，没有复制或简化其匹配逻辑。

Rust 扩展需要 Cargo／rustc。本实验锁定 `memchr 2.8.3` 和 `aho-corasick 1.1.5`，两者上游均为 MIT／Unlicense 双许可，Cargo.lock 保存来源与校验和。初次下载由使用者显式执行，之后 CMake 只允许离线、锁定版本构建：

```bash
cargo fetch --locked --manifest-path tools/benchmarks/literal_search/rust/Cargo.toml
cmake -S tools/benchmarks/literal_search -B out/benchmarks/p11-literal/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/gcc_64 \
  -DMIMALLOC_LIBRARY=/absolute/path/to/out/build/p6-boolean/output/libmimalloc.a \
  -DLITERAL_ENABLE_RUST=ON
cmake --build out/benchmarks/p11-literal/build --parallel 8
python3 scripts/dev/benchmark_literal_search.py \
  --binary out/benchmarks/p11-literal/build/literal_search_benchmark \
  --candidates std qstring memchr aho qt --rounds 4
```

依赖来自 Cargo 缓存，仅属于实验，不纳入应用 vendor 或构建链。Rust 不足以证明实际收益时不升级为生产依赖。

StringZilla 是另一项独立开关，不需要 Rust。实验固定 v5.3.2、提交 `1c78fe5d288e3abb3bd1583f291c4f3d292133d4`，许可证 Apache-2.0。显式下载并核验本次记录的存档摘要后，将解压目录传给 CMake：

```bash
mkdir -p out/benchmarks/p11-stringzilla
curl --fail --location \
  https://codeload.github.com/ashvardanian/StringZilla/tar.gz/1c78fe5d288e3abb3bd1583f291c4f3d292133d4 \
  -o out/benchmarks/p11-stringzilla/source.tar.gz
echo 'd167cc75343369771b83d44ddd9ff0e43e689a7aa99ed39f3167680d8d85d91d  out/benchmarks/p11-stringzilla/source.tar.gz' | sha256sum --check
tar -xf out/benchmarks/p11-stringzilla/source.tar.gz -C out/benchmarks/p11-stringzilla
cmake -S tools/benchmarks/literal_search -B out/benchmarks/p11-stringzilla/cpp -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/gcc_64 \
  -DMIMALLOC_LIBRARY=/absolute/path/to/out/build/p6-boolean/output/libmimalloc.a \
  -DLITERAL_STRINGZILLA_SOURCE_DIR="$PWD/out/benchmarks/p11-stringzilla/StringZilla-1c78fe5d288e3abb3bd1583f291c4f3d292133d4"
cmake --build out/benchmarks/p11-stringzilla/cpp --parallel 8
python3 scripts/dev/benchmark_literal_search.py \
  --binary out/benchmarks/p11-stringzilla/cpp/literal_search_benchmark \
  --candidates stringzilla stringzilla-serial qt \
  --output out/benchmarks/p11-stringzilla/results
```

存档摘要是本次获取的内容指纹，不是上游签名。配置额外核验所有 `include/stringzilla/**/*.h` 和 `LICENSE` 的固定摘要，文件变化会触发重新核验；CMake 不联网获取依赖。只使用查找所需头文件，不编译上游完整动态库。源码与结果均留在忽略的 `out/` 下。

需要直接比较两个候选时，在同一构建同时打开 `LITERAL_ENABLE_RUST=ON` 与 StringZilla 来源选项，然后运行：

```bash
python3 scripts/dev/benchmark_literal_search.py \
  --binary out/benchmarks/p11-stringzilla/build/literal_search_benchmark \
  --reference memchr --candidates stringzilla memchr \
  --output out/benchmarks/p11-stringzilla/direct-memchr
```

`--reference` 默认为 `qt`；选择其他参考时，仍在计时外另用真实 Qt 校验完整位图。候选与参考相同即为同实现控制，原始数据和汇总均记录参考后端。

## 实现与正确性边界

- `qt`：真实默认 Qt 正则匹配器，逐行输出每个模式的命中。
- `qstring`：Qt 解码一次后，逐模式执行区分大小写的 QString::contains。
- `std`：验证 UTF-8 后，逐模式执行 std::string_view::find。
- `memchr`：构造可复用的 memmem::Finder，通过一次批量 FFI 扫描全部合格行。
- `stringzilla`：在支持的 GCC／Clang x86_64 上检查 AVX2、BMI、BMI2、LZCNT、SSE4.2 后调用公开 Haswell 查找内核，否则使用串行内核；JSON 的 `implementation` 记录选择结果。没有全局 `-mavx2`／`-march=native`。
- `stringzilla-serial`：始终调用公开串行内核。上述分派只用于实验，MSVC 及 ARM 当前仅走串行路径，Windows／macOS／ARM 尚未做原生验证；不是完整生产分派方案。
- `aho`：AhoCorasick 的 Standard 模式，枚举重叠匹配并按原始模式 ID 标记；全部模式已命中时可以提前结束。不能用只返回左侧非重叠匹配的 API 代替，否则会漏掉子模式和重复模式。

仅全部模式均为非空、区分大小写、有效 UTF-16 的纯文本查询时启用候选；其他查询整体回退 Qt。字节候选每轮扫描逐行验证 UTF-8，对异常字节与行首 BOM 回退。QString 候选保留 Qt 的解码替换行为。没有自行实现 Unicode 大小写折叠，也没有将忽略大小写悄悄降为 ASCII。

`--verify` 对照 Qt 的完整结果位图，覆盖重复／重叠／包含模式、UTF-8、Unicode 忽略大小写回退、NUL、空模式、空行、空模式集合、无效 UTF-8、BOM、截断字节视图、混合无效 UTF-16 查询、有效 UTF-16LE 转码样本及 1,024 条确定性随机输入。无效查询测试只局部过滤预期的 Qt 警告。另覆盖 1–513 字节关键词、0–63 字节地址偏移、SIMD 长度边界；Linux 使用相邻不可访问页，检查页尾前 0–32 字节命中、未命中及内部字节不匹配。它不测试 UI 高亮偏移或完整搜索管线。

## 测量方法

每批 128 行。常规矩阵为 3 类输入（ASCII／有效 Unicode／含非法字节）× 2 种行长度（128／4096 字节）× 4 种模式数（1／4／16／64）× 2 种基础关键词长度（4／24）× 3 种目标命中率（0／50／100%），共 144 场景。Unicode 关键词前置“文”，实际长度为 7／27 字节；JSON 同时记录基础长度和实际 UTF-8 最短／最长长度。随机背景可能自然命中短关键词，因此另记录实际命中行数与位图中命中单元数。

另外 3 个压力场景：64 个高度重叠模式中一个始终未命中，以及 1／64 个长公共前缀关键词全部未命中，总共 147 场景。可用 `--case ascii-128-p1-n4-h50` 单独运行或传给 runner。UTF-16／其他文件编码的转码不在计时内：实际匹配器看到的是 `RawLines::buildUtf8View()` 的输出。本工具的 UTF-16LE 样本只证明有效文本转换后的匹配一致。

每个场景分别创建所选参考与候选，记录构造时间及首批扫描（包括 Qt 延迟编译／JIT）；之后双方预热，根据预热耗时选择 1–4096 遍，目标约 5 ms，按 ABBA 顺序热扫描；汇总倍率为参考样本中位数除以候选样本中位数。runner 默认 4 轮，交替构造／测量顺序和候选进程顺序，每个场景每侧有 8 个热样本、4 次独立构造／首次扫描。构造和首次数据在同轮的两条热样本中重复记录，不是 8 次独立冷启动。进程内较晚的场景也已受益于库初始化，不能视为全新进程启动时间。

Linux runner 固定在允许集合的第一个 CPU，可用 `--cpu` 修改，但不锁频；其他平台不固定 CPU，元数据会显示空值。不要同时执行编译或其他性能负载。`--reference qt --candidate qt` 是相同实现的控制实验，仍可能受 PCRE2 JIT 代码位置、内存布局和系统噪声影响；小于控制波动的收益不能据此采纳。

计时包括 UTF-8 验证、回退、批次结果分配／写入、Rust FFI 适配以及完整位图 checksum 遍历。Qt 基线仍每行产生生产 MatchedPatterns，候选直接填充批次位图；因此结果同时反映算法与批量接口的变化，不能当作单个库函数加速，也不能外推为整应用搜索倍数。Rust 每批一次 FFI，若接入逐行生产接口，必须重新测量。StringZilla 每个模式调用无状态查找，memchr 复用构造好的 Finder；两者准备成本和接口不同，不能只用热扫描评判首次结果。

`automaton_bytes` 仅为 aho-corasick 报告的自动机内部内存；其他后端的 0 表示未报告，不能解释为无分配。未测峰值 RSS、完整分配成本、取消响应、磁盘读取、预过滤、布尔求值、高亮偏移、UI 或真实用户日志。当前 FFI 只接收固定规模的内部测试数组，不是可供不可信调用者使用的公共 ABI。

runner 保存原始 JSONL、stderr、验证日志、平台／CPU、编译缓存、Rust 版本、二进制与源码 SHA-256，以及逐场景中位数。只有全部运行成功且输入／结果摘要一致，才生成 `results.json`；速度不作为 CI 通过条件。结果目录默认 `out/benchmarks/p11-literal/results`，可用 `--output` 指定。

首轮结论与未完成条件见 [P11 记录](../../../docs/superpowers/plans/2026-11-16-literal-search-evaluation.md)。

StringZilla 后续结果、源码／二进制成本及保留生产实现的决定见 [补充记录](../../../docs/superpowers/plans/2026-11-16-stringzilla-evaluation.md)。
