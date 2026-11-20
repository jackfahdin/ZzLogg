# 搜索提交线程协助执行计划

> 使用 executing-plans 内联执行，最后一次新鲜上下文独立审查。

**目标：** 用现有提交线程执行已接纳图任务，验证能否改善有限 TBB 预算下的搜索。
**规格：** [实验设计](../specs/2026-11-20-search-producer-assist-design.md)。延续用户对上一轮下一步的“继续”授权。
**架构／技术栈：** C++／Qt／oneTBB，编译隔离的实验策略，Python 真实子进程验证与配对测量。

## 全局约束

原 Qt／轮询默认不变；limiter 保持 matchingThreadsCount × 3；不新增依赖、查询特判、系统权限或 Windows 测试。本地 master 中文提交，不推送。

## 审查重点

- assist 命名与实际策略一致，Qt 基线不会残留协助模式或启用其他正则后端；真实 profile 与完整结果测试覆盖。
- 容量满后协助执行可能抛异常，仍必须在节点析构前排空；六种注入、准确注入计数和再次搜索覆盖。
- 预算 1／2／3、取消、尾部追加、范围搜索保持完成信号和结果语义；运行现有完整契约三种调度。
- 实际提交线程回调计数只用于独立 profile，不把图等待次数当成匹配数量或活跃线程数；大于队列容量的 fixture 验证实际参与。
- 小批次可能降低吞吐量，保留全部 8 场景、四轮交错与 Qt 控制；原独立 oracle 验证计时和 profile。

### Task 1: 实验策略、真实契约和同机三方测量

**文件职责：** `src/logdata/src/logfiltereddataworker.cpp` 的宏内分支执行策略；`tools/benchmarks/search_worker/{experimental.h,experimental.cpp,main.cpp,recovery.cpp,harness.h,contract.cpp}` 负责实验控制、诊断和真实契约；`scripts/dev/{benchmark_search_worker.py,verify_search_tbb_limit.py}` 及诊断单测负责输入、透传和结果验证。README、路线图和本文记录结果。

独立 Android oracle 的 `scripts/dev/utf8_worker_corpus.py` 和 `tests/ci/test_utf8_worker_corpus.py` 同时支持 assist 标签，保持原固定结果／路由／取消校验。

- [x] 扩展真实预算契约到 assist，加入超过队列容量的固定结果 fixture，验证实际提交线程回调和 assist 模式；runner 单测拒绝错误调度记录。运行旧 probe／旧 runner，Expected: 不认识 assist／校验函数缺失，RED。
- [x] 添加互斥实验调度枚举、宏内协助分支、profile 计数和 CLI／runner 支持，原 verify 覆盖三个模式。Expected: 两种 probe 的预算／故障契约 GREEN。
- [x] 构建默认 ci_build、两种 probe，跑默认 CTest、Python CI 与外部语料契约。Expected: 全通过。一次独立审查，Important 先复现再修复。
- [x] 无并发构建／测试时，在固定 CPU 上交错执行三种预算、assist／wake 每组四轮配对与独立 profile，保留原始记录并核对 oracle／源码摘要。Expected: 完整结果一致；性能不预设通过阈值。
- [x] 文档记录收益、退化与结论，中文本地提交并归档验证日志。任务最终验证：`python3 -m unittest discover -s tests/ci -p test_search_worker_diagnostics.py`。

## 实现和验证结果

实验控制使用互斥 `Scheduling::{Poll,Wake,Assist}`，每次配对搜索都重新选择策略，assist 仍使用 Qt 匹配器。生产 worker 的新行为完全位于实验宏内；默认 archive 检查无 `experimental::` 符号，原 limiter 和异常排空路径未改。

通过提交线程的线程局部作用域统计实际匹配／合并回调，不把发起等待当作已经执行。`producer_drains` 记录容量不足后的图等待次数；`producer_callbacks` 包含最后一次正常 join 的回调，所以不代表匹配节点数量或只在容量等待时做的工作。

真实 CLI 契约使用 257 行、129 个偶数命中、最大长度 5 的独立固定预期，超过 limiter 容量；预算 1 时明确要求实际图等待和提交线程回调均非零。原完整 verify 扩展至三种调度，覆盖取消、重搜、范围、尾部追加与高亮路由。每种 probe 构建另跑六处故障 × 三种调度 × 三种预算，共 **54 个组合**，每组覆盖初次搜索与更新搜索的失败、准确故障注入及恢复。

先观察旧 probe 拒绝 assist、runner 缺实际策略校验、独立 oracle 拒绝新策略的失败，再实现并验证。默认 ci_build、PCRE／memchr probe 均构建成功；离屏 **99/99 CTest**、Python **149 项（3 项跳过）**、两种真实预算／故障契约、外部文件语料契约全部通过。一次全新上下文只读审查：Critical／Important／Minor 均无；审查未预判性能，本节后续以实际对照回答。

## 固定硬件的对照

同一最终二进制，CPU **0／2／4** 三个独立 P 核，TBB 上限 **1／2／3**；8 个既有 Android 合成场景，各 65536 行、chunk 1024。每个预算先 assist 再 wake，各跑两轮；第二块反转全部预算／策略组顺序，各再跑两轮。每组包含 ABBA／BAAB 与 Qt／Qt 控制，诊断使用独立进程。完整保留 **1536 条计时和 384 条 profile**，原独立 oracle 全部通过；每行默认并行度为 3、实际全局预算符合组值。源码、二进制、外部 PCRE 摘要及合并中位数已校验。

环境：Linux、i7-14700、Qt 6.11.1、GCC 15.2、Release、LTO OFF，未锁频。正式测量没有并发构建／测试。各策略的比较都使用自己的同轮 Qt 基线，不直接拿不同进程的绝对耗时相除。

| 查询 | 上限 | 轮询 ms | assist ms | 轮询／assist | CPU 轮询→assist ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| dmesg-boolean-t2 | 1 | 17.209 | 16.977 | 1.014 | 17.264→16.826 |
| dmesg-boolean-t2 | 2 | 15.364 | 7.912 | 1.942 | 16.857→15.578 |
| dmesg-boolean-t2 | 3 | 11.547 | 7.700 | 1.500 | 17.684→20.547 |
| app-multi16-t2 | 1 | 179.326 | 178.017 | 1.007 | 181.460→179.284 |
| app-multi16-t2 | 2 | 160.075 | 109.029 | 1.468 | 170.016→207.710 |
| app-multi16-t2 | 3 | 100.125 | 98.217 | 1.019 | 200.446→199.830 |

上限 1 时所有调度都因没有后台名额而执行同一协助分支，全部 64 个块的 128 次匹配／合并回调在提交线程执行。多数汇总接近持平；boolean-inverse 单节点仍有约 6% 差异，个别轮次更大，不能将这种相同算法路径的波动解释为调度收益。

上限 2 时，assist 的目标布尔查询四轮均加速（1.956／1.829／1.816／2.223），汇总约 **1.94 倍**、CPU 减少约 **7.6%**。multi16 双节点也四轮加速（1.558／1.759／1.444／1.568），汇总约 **1.47 倍**，但 CPU 增加约 **22.2%**。该组两个场景的提交线程回调中位数都是 **64**，容量图等待中位数 **10**，通知等待和轮询 sleep 都为零，确实利用了已有提交线程；收益不是增加全局预算获得的。

上限 3 时，布尔双节点四轮均加速，汇总约 **1.50 倍**，但 CPU 增加约 **16.2%**。multi16 双节点四轮倍率 **1.048／0.933／1.016／1.097**，汇总只有 1.019，不能宣称稳定变快。multi16 单节点基本没有 wall 收益，预算 2／3 的 CPU 分别增加约 **17.7%／14.4%**。

| 全部 assist 场景 | 上限 1 倍率 | 上限 2 倍率 | 上限 3 倍率 |
| --- | ---: | ---: | ---: |
| app-multi16-t1 | 0.989 | 0.983 | 1.005 |
| app-multi16-t2 | 1.007 | 1.468 | 1.019 |
| dmesg-boolean-inverse-t1 | 0.943 | 1.413 | 1.393 |
| dmesg-boolean-inverse-t2 | 0.995 | 1.939 | 1.409 |
| dmesg-boolean-t1 | 1.010 | 1.573 | 1.566 |
| dmesg-boolean-t2 | 1.014 | 1.942 | 1.500 |
| dmesg-errno-t1 | 1.019 | 2.667 | 2.622 |
| dmesg-errno-t2 | 0.978 | 2.356 | 2.492 |

本轮 wake 在预算 2 的布尔双节点／multi16 双节点汇总分别为 **1.042／0.993**；预算 3 分别为 **1.532／0.997**。后者重正则的四轮倍率跨越 1（0.892／0.965／1.031／1.114），因此上一轮的约 10% 退化没有在本轮稳定复现，也不能说被 assist 解决了。

### 波动与结论边界

| 预算 | assist 组 Qt 控制汇总范围 | wake 组 Qt 控制汇总范围 |
| --- | ---: | ---: |
| 1 | 0.970～1.021 | 0.987～1.031 |
| 2 | 0.959～1.020 | 0.985～1.106 |
| 3 | 0.962～1.011 | 0.894～1.003 |

单轮 Qt 控制的总体范围更宽：assist **0.785～1.166**、wake **0.816～1.342**。所有样本保留，不能只报合并中位数掩盖波动。两名额布尔查询的多轮大幅收益值得保留，但小幅正负差异不足以定论；CPU 增量也是本机本批次观察，不外推到所有平台。

全部主要语料的取消诊断均实际请求并提前停止，资源最终归零；这证明取消功能可用，不证明取消延迟改善。没有测出精确的锁／自旋热点或实际并发匹配器数量，也没有额外 Windows 验证。

## 决定

**继续原 Qt／轮询默认，assist 保留为显式实验。** 它验证了名额不足时提交线程参与的价值，但完整排空小批次会改变读入／匹配重叠，且 CPU 成本、重正则稳定性没有同时达标。不按查询名称、正则内容或本轮观测添加自适应规则，不扩大 limiter 或全局预算。

这轮先收敛调度实验，不继续叠加新的调度策略。若之后申请产品准入，优先在更稳定的环境复核现有候选的 wall／CPU／取消取舍，证据不足时维持当前默认。

## 可复现产物

位于 `out/benchmarks/p12-producer-assist/`：`input/` 保存既有输入清单与独立预期；`run-final.py` 执行交错矩阵并保留两个原始 block；`report.py` 核对摘要和统计；`final/<策略>-limit-<预算>/results.json` 为合并报告；`validation/` 归档本轮验证与审查账本。

测量基线 `8995bdc1df1acaf938f29bb45b7865d23311a347`，使用本轮实际工作树，报告记录源码摘要。最终二进制为 `out/build/p12-utf8-worker/output/zzlogg_search_worker_probe`，SHA-256：`f87df871db538a47138cc4955667bc93d88d0ac2336e729c36b1a523a0aee064`。

| 报告 | SHA-256 |
| --- | --- |
| assist-limit-1 | `280d8948d9c5ae23af570e61ffa4623347ab6131926e70bb5fbedfd0208879eb` |
| assist-limit-2 | `afd77b911fad591bfe285e0e11700884a987604044ed9e726fac5cbad5c5daa9` |
| assist-limit-3 | `3f4aaae8d9c5284858b80bd1b079f4a6a1474da5c96546c7b245b6642b3bc03a` |
| wake-limit-1 | `46cdad3142324dcd40cf9e4dc2a5dd8bf89301e8b9d504d6088d5dac11166a36` |
| wake-limit-2 | `7c2a1fc6f0db224f1e551594096b8e4d333328436b60084520f7352d217c3ca7` |
| wake-limit-3 | `fd0b39972e88d404de44ba9538fc84879fa48fc11e7800525db7a32256c8a753` |
