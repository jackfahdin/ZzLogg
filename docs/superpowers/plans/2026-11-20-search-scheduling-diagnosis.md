# 搜索通知额外开销诊断计划

> 使用 systematic-debugging 定位原因，writing-plans／executing-plans 内联执行，最后一次新鲜上下文只读审查。

**目标：** 区分 CPU 拓扑、可用执行名额和通知唤醒的影响，避免凭壁钟差值猜测锁瓶颈。
**规格与方案：** 延续 [生产准入评估](2026-11-20-production-search-wakeup.md) 的下一步。先用未修改的 probe 比较同核 SMT（0／1）、两个独立 P 核（0／2）、三个独立 P 核（0／2／4），保持语料／查询／chunk／匹配节点数和配对顺序一致。随后仅扩展诊断工具，采集同一搜索区间的 user／system CPU 和自愿／非自愿上下文切换；记录 CPU 拓扑及 TBB 全局名额。每轮 ABBA／BAAB 加 Qt 控制，使用固定独立 oracle。

## 边界

默认 Qt／轮询不改。只修改显式 probe、runner、测试和文档；不新增依赖，不提高系统 perf 权限，不追加 Windows 验证。perf_event_paranoid=4 拒绝 perf，所以不声称取得调用栈热点或精确锁耗时。用户态 CPU 与上下文切换仅作证据，不能相互替代。沿用本地 master 中文提交授权，不推送。

## 文件与接口

- `scripts/dev/benchmark_search_worker.py`：元数据记录选定 CPU 的 package/core/siblings；未知信息为 null，不把逻辑 CPU 数当物理核数。校验新资源字段齐全、非负、user+system=total；保留旧格式报告兼容。
- `tools/benchmarks/search_worker/harness.{h,cpp}`：用原有两次 getrusage 同时捕获 CPU 分类和切换计数，无新热循环探针。不支持或读取失败统一 -1。
- `tools/benchmarks/search_worker/main.cpp`：计时行输出资源增量，profile 输出 TBB default concurrency/global limit；不据此伪称实际 matcher 并发数。
- `tests/ci/test_search_worker_diagnostics.py`：人工拓扑区分 SMT、独立核／不同 socket、未知值；拒绝不完整或不自洽资源记录。
- 既有 probe 契约：完整结果之外检查资源数据可用时自洽，unsupported 时一致 -1。

## 审查重点（Review Focus）

- 两次资源采样仍包住同一 submit→结果收集→销毁/join 区间，不能把进程启动／索引计入搜索成本。
- Linux sysfs 不可读、core/package未知或不同socket同core编号不产生错误物理核数。
- 统计字段不能混淆累计计数、区间增量、wall、process CPU、独立 profile 与实际执行线程数量。
- 同一最终二进制按拓扑顺序交错运行，保留初始与所有正式结果，不丢弃退化样本。
- 不将拓扑相关性直接升级成锁竞争／代码热点的精确归因；不因有局部收益就默认启用。

### Task 1: 可复现的调度诊断

- [x] 初始原二进制三种拓扑各四轮、8 场景；独立 oracle 校验。Expected: 结果一致，建立未加新诊断前的证据。
- [x] 写拓扑及资源记录测试，先在缺失实现时失败；实现新元数据和 getrusage 增量输出，固定 probe 契约检查一致性。Expected: RED→GREEN，旧报告仍能验收。
- [x] 默认 ci_build／完整 CTest／Python CI、memchr／PCRE worker 契约与外部输入契约通过；一次独立审查。
- [x] 最终 probe 三种拓扑交错、四轮、8 场景，核对每条完整结果、查询／文件身份、CPU字段与元数据。Expected: 共 768 计时／192 独立 profile；无并行构建或测试负载，数据按拓扑各自总结，不作跨核绝对耗时的单因素推断。
- [x] 记录证据、归因边界和下一步，更新路线图与工具说明，中文本地提交。产物保存 `out/benchmarks/p12-scheduling-diagnosis/`。

## 交付与验证

仅扩展显式 probe 和 runner，生产 `src/`、匹配算法和通知门均未修改。资源采样沿用原来的两次 getrusage，同时获得 user／system CPU 和自愿／非自愿上下文切换增量，没有增加逐行／逐块计时探针。全部新字段支持旧输出兼容及统一 unsupported=-1；Linux 本轮实际数据均有效。

新增 Python 测试先在缺失实现时失败，再通过。独立只读审查指出重复 CPU 参数会被 affinity 去重、却被元数据误记为 SMT；按诊断准确性问题处理，新增回归观察 `2 != 1` 后统一去重。最终 **99/99 CTest**、Python **147 项（3 项跳过）**、默认 ci_build、memchr／PCRE 完整 worker 契约及外部语料／快照／错误契约通过。审查没有其他问题或搁置项，不保留已知的重复 CPU 误判。

## 已确认的执行条件

本机 CPU 0／1 是一个物理 P 核的两个 SMT 线程；CPU 0／2 是两个独立 P 核，CPU 0／2／4 是三个独立 P 核。sysfs 的原始 core_id 分别为 0／0、0／4、0／4／8，package_id 均为 0；它们是身份编号，不要求连续。

独立审查核对仓库 oneTBB：`src/tbb/global_control.cpp` 与 `src/tbb/threading_control.cpp` 中，常规后台 worker 软预算为全局并行上限减一，保留外部线程参与执行的名额。全局上限 2 并不等于有两个后台 matcher 持续执行；上限 3 才允许常规最多两个后台 worker。强制执行需求存在特殊例外，所以这里只描述常规预算，不能把它当作实际活跃线程计数。

本项目的提交线程来自 QThreadPool，满队列时通知分支在条件变量等待；此前只在单执行名额时主动参与图任务。因此在两个逻辑 CPU 的测试环境中，配置两个匹配节点并不保证充分的双节点后台并行。新 profile 已实际记录本轮默认并行度／全局上限分别为 2、2、3。

## 对照方法

使用同一份 Android **模拟** dmesg 和应用日志，8 个场景：errno、boolean、boolean-inverse、multi16，各 1／2 个匹配节点；每文件 65536 行，chunk 1024。输入是前轮固定 oracle 的子集，没有按本轮结果挑选查询。

初始未改 probe 对三种拓扑各测四轮，合计 **768 条计时、192 条独立 profile**，全部独立校验。基线中 `dmesg-boolean-t2` 在 SMT／两个独立核下通知倍率 0.980／0.948，三个独立核下为 1.590，重正则双节点的轮询耗时从约 162／155 ms 降到约 98 ms。这些结果保留在 `baseline-smt`、`baseline-cores`、`baseline-three-cores`，不与新二进制绝对耗时混算。

最终 probe 按 SMT→两个独立核→三个独立核各两轮，再反序各两轮；每组使用 ABBA／BAAB 以及 Qt 同实现控制。所有原始分块报告保留，合并时只为第二块的轮次加 2，核对两块元数据完全一致，重新计算中位数。合计另有 **768 条计时、192 条独立 profile**，完整结果／输入身份／查询身份／路由／资源字段全部通过独立校验。没有并行构建或测试负载；Linux、i7-14700、Qt 6.11.1、GCC 15.2、Release、LTO OFF、未锁频。

## 最终观察

以下均为完整 worker 区间的中位数，CPU 是所有进程线程的 user+system 增量，自愿切换是计数而非耗时。

| dmesg-boolean-t2 环境 | Qt 轮询 ms | Qt 通知 ms | 轮询／通知 | 进程 CPU 轮询→通知 ms | 自愿切换轮询→通知 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 同核 SMT，上限 2 | 17.645 | 15.973 | 1.105 | 19.445→18.023 | 82.5→127.5 |
| 两个独立核，上限 2 | 16.828 | 16.379 | 1.027 | 18.389→17.736 | 80.5→126.5 |
| 三个独立核，上限 3 | 11.620 | 7.590 | 1.531 | 17.904→16.151 | 103→128.5 |

最终各拓扑中该查询四轮通知的配对 wall 中位数都较低；先前约 8% 的退化没有在这次完整诊断中复现。初始未改 probe 仍曾复现小退化，且两轮之间重正则的绝对耗时明显变化，因此必须保留环境／频率／调度波动这一限制。不能把新增资源字段前后的跨轮差异当作诊断代码加速，也不能把旧退化删掉。

通知每次容量释放都可能唤醒提交者，`dmesg-boolean-t2` 的通知等待中位数为 55.5／58／54.5 次，轮询 sleep 为 16／15／10 次。自愿切换增加，但该查询内核态 CPU 中位数反而从 0.997→0.517、1.508→0.997、0.535→0.454 ms；总体 CPU 同样下降。**因此“唤醒多，所以一定花更多 CPU”没有得到本轮证据支持。** user／system 等各字段分别取中位数，不能将各自中位数相加当作总 CPU 中位数。

三个独立核下 errno 双节点通知倍率为 2.543，boolean-inverse 双节点为 1.427；但 multi16 双节点为 **0.903**（轮询 105.474 ms、通知 116.767 ms，通知／轮询 CPU 为 **1.102**）。Qt 同实现控制倍率范围在 SMT／两个独立核／三个独立核分别为 **0.957～1.006／0.999～1.039／0.998～1.022**。更充分的执行资源并未让所有查询受益，不据此默认开启通知。

## 结论与下一步

已确认此前“CPU 0／1 + 两个匹配节点”的测试不能被解释为两个独立物理核或两个持续运行的后台 matcher。执行名额和硬件共享是评估通知成本必须记录的条件；通知的收益／退化不是某个布尔表达式的固定属性，也没有证据将额外 CPU 精确归因到 gate mutex。

本轮仍然同时改变了硬件资源和 TBB 自动预算，不能声称已经单独量化两者各自贡献。perf 在当前权限下不可用，未取得调用栈热点或锁等待时间。下一步应在**相同物理 CPU 集合**上独立控制 TBB 上限，必要时再对提交线程协助执行作显式实验；先完成单变量归因，再决定是否调整通知策略。生产继续原 Qt／轮询。

### 可复现产物

- 输入／独立预期：`out/benchmarks/p12-scheduling-diagnosis/input/`。
- 最终交错运行及合并脚本：`out/benchmarks/p12-scheduling-diagnosis/run-final.py`，直接从仓库根目录执行；每拓扑两个两轮块，全部命令和原始输出保留。
- 最终二进制：`out/build/p12-utf8-worker/output/zzlogg_search_worker_probe`，SHA-256 `92aaa4126cf20745e54bf17c0d90a5d59d409b56c1f70a1bb66ba7e2c247e837`。
- 最终 SMT 报告 `final/smt/results.json`，SHA-256 `c36cfb89496d9d27017a4f2c5e7d73b36727465df530a6932de3b922bec5830e`。
- 最终两个独立核报告 `final/cores/results.json`，SHA-256 `4d9399fe17c96075c0eed7c1171f35bbe49b46c1e260332fb77e126ecd0a21d7`。
- 最终三个独立核报告 `final/three-cores/results.json`，SHA-256 `de80782d0fc0dbfa8c2933234031c04091610310ec9183da0ea894e20051cba1`。
- 验证日志 `out/benchmarks/p12-scheduling-diagnosis/validation/`；最终报告的源码、外部 PCRE 和二进制摘要已核对。

报告相对路径均位于 `out/benchmarks/p12-scheduling-diagnosis/`。测量基线为 `1576e7103cb3e2ff116712168c0c4c381de59ce1`，实际使用本轮工作树；不追加用户已取消的 Windows 验证。
