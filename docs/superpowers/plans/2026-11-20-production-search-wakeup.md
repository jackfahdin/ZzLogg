# 搜索容量通知生产接入实现计划

> **面向 AI 代理的工作者：** 使用 executing-plans 内联执行，最后进行一次新鲜上下文独立审查。

**结论：** 通知共用实现与默认契约已完成；因双线程布尔负载退化，生产继续原轮询。

**目标：** 共用通知实现，验证生产默认接入的正确性和收益。
**规格：** [生产接入设计](../specs/2026-11-20-production-search-wakeup-design.md)。
**技术栈：** C++17、Qt、oneTBB、标准库条件变量、Python。

## 全局约束

沿用本地 master 中文提交授权，不推送。保留匹配后端和容量限制，不新加依赖、UI 设置或生产测试钩子。不追加 Windows 验证。

## 审查重点（Review Focus）

- 默认和实验不得各维护一份不同的通知算法；完整匹配／取消／追加更新都走同一释放路径。
- gate 同锁内 try_put／decrement 必须不重入释放，避免死锁；释放只发生一次，早退不漏。
- 单执行名额仍能推进，正在等待时观察取消／TBB 异常取消；图节点析构前排空。
- 等待测试不能把很短调度延迟当正确性；外部硬限时能够终止停滞。
- 最终测量对应交付代码，不把模拟语料或 Linux 数据当成所有负载／平台的保证。

### Task 1: 共用通知门并默认接入

**文件：** 新增 `src/utils/include/searchcapacitygate.h`、`tests/ui/search_capacity_gate_test.cpp`；修改 utils／tests CMake、`src/logdata/src/logfiltereddataworker.cpp`、`tests/ui/search_worker_recovery_test.cpp`、实验 `wakeup.h` 和文档。

- [x] 先增加默认 gate 契约：先用现有实验门编译运行，临时移除 notify_one，确认拒绝后释放契约不能在 2 秒内完成（门的兜底超时为 5 秒，RED）；还覆盖提前释放、无通知取消、4096 次容量 3 的并发提交。进程硬超时 30 秒。
- [x] 将原门移入 utils 并保留类型别名，生产 scope guard 释放，移除生产轮询／decrementer 边，仅在实验保留参考路径；原门恢复 notify（GREEN）。默认 worker 测试增加 global_control(2) 的同 worker 复用序列。
- [x] 默认 ci_build、完整离屏 CTest／Python CI；单 CPU 默认测试；显式 probe 两调度固定契约、单／双 CPU 故障矩阵、实际 Vectorscan 集成；默认 archive 不含 experimental 符号。Expected: 全部通过，原默认测试无退化。
- [x] 一次只读独立审查，重要问题按失败测试修复。无并行构建／测试负载时，最终代码复跑原 30 场景四轮 Android 模拟对照及独立 oracle。Expected: 960 计时 + 240 诊断完整一致，评估 wall／CPU／控制组再决定默认采用。
- [x] 更新路线图、README、历史文档后续指向和本计划实测结论，中文本地提交。验证日志保留 `out/benchmarks/p12-production-wakeup/validation/`。

## 临时默认通知接入的正确性结果

默认构建已接入通知门，Qt／加速匹配选择不变。同步门移入 utils，实验头仅别名；只在实验副本编译旧轮询与原 decrementer 连边。默认结果回调的 scope guard 在正常返回和取消早退时释放容量，异常路径仍由 isolated context 取消并在节点存活时排空。

仓库内 oneTBB 的 limiter 无可回拉前驱，decrement 只更新容量而不会进入消费 body；后继是 buffer，try_put 只入队。同步门持锁调用这两个入口不重入 release。单执行名额分支不持门锁执行图，避免本线程等待自己释放容量。

默认 `ci_build`、离屏 **99/99 CTest**、Python **141 项（3 项跳过）**、CPU 0 默认 worker 和两个目标各 20 次重复通过。memchr／PCRE 完整 worker 契约，两种调度均通过；6 处故障 × 两调度在 PCRE 构建的 CPU 0 与 CPU 0／1 均通过。真实 Vectorscan 高亮／路由在两种调度通过，该构建的双 CPU 故障矩阵也通过（故障模式选择 Qt）。默认 archive 不含 experimental 符号，worker 对象使用条件变量通知且不再引用 nanosleep。

外部文件／快照／错误及超时契约通过。第一次误将只支持 memchr 候选的契约脚本指向 PCRE 构建，报 `Unknown backend`；改用匹配的 memchr probe 后通过，没有修改脚本来放宽验证。

### 通知测试的纠正

临时删除 notify 后，旧测试竟能通过：predicate wait 到截止时发现释放代数已变化，也返回 true，因此 `timeouts == 0` 并不等于收到了通知。本轮在默认测试和 probe 契约都增加 future 2 秒内就绪检查，测试门的兜底超时设为 5 秒。删除 notify 后以退出码 1 和 `Capacity release did not wake producer before fallback` 真实失败，恢复后通过。

这是作者作出的测试裁决：用宽松、有明确先后关系的两个截止时间验证活性，不把超时计数充当通知证据。代价是极端停调度的 CI 仍可能失败；不以无界等待掩盖停滞。生产超时保持 1 ms，不受测试的 5 秒参数影响。

## 默认采用裁决

首次完整评估的 960 条计时／240 条诊断全部通过独立 oracle，但 `dmesg-boolean-t2` 四轮 wall／CPU 均退化，汇总轮询 15.186 ms、通知 16.372 ms（增加约 7.8%），进程 CPU 增加约 10.8%。同实现主日志控制倍率 0.980～1.003，不能将该差异全部归为随机波动。原始报告保留 `out/benchmarks/p12-production-wakeup/evaluated-default-on/results.json`，SHA-256 `d59df0ac00763a835c447e1e3cc05f9e25281961614acc515de4122926c63419`。

作者裁决：**回撤默认启用的生产改动，继续原轮询。** 不为保留个别场景收益而增加按查询类别的特判和双默认策略；保留共用通知门、默认测试与评估数据。这优先满足不强行替换的约束，代价是普通单线程 dmesg 的通知收益尚未进入默认应用。不是取消优化方向，后续应先解释双线程布尔负载的额外 CPU 成本，而不是继续添加依赖。

最终提交不改变 `logfiltereddataworker.cpp`，以下将记录恢复默认后的完整回归及最终源码对照；上方临时默认启用证据保留为评估历史。

## 最终源码复测与交付

恢复默认轮询后，重新构建全部受影响目标，默认 `ci_build` 与离屏 **99/99 CTest**、Python **141 项（3 项跳过）**再次通过。CPU 0 默认 worker、memchr／PCRE 完整契约、PCRE 单／双 CPU 六处故障 × 两调度、实际 Vectorscan 路由／故障及外部语料契约通过。默认 archive 无 experimental 符号，生产 worker 源文件与基线完全一致。

一次新鲜上下文只读独立审查无 Critical／Important／Minor 或搁置项，独立核对 oneTBB 不重入释放的条件并运行门契约。审查后仅撤回生产默认启用改动，通知门与测试算法未变；没有为保留通知收益加入查询特判。

最终源码重新运行相同 30 场景、四轮 ABBA／BAAB 和 Qt 同实现控制：**960 条计时 + 240 条独立进程诊断**全部通过独立 oracle。全部摘要中位数重新计算，二进制、相关源码、外部 PCRE 源码摘要均核对一致。默认恢复前的报告完整保留，不以最终轮覆盖不利样本。

环境为 Linux x86_64、i7-14700、CPU 0／1、Qt 6.11.1、GCC 15.2、Release、LTO OFF、未锁频；正式计时无并行构建／测试。数据是 Android **模拟**日志，不是用户私有日志；比较的是显式 worker 副本内的 Qt+通知与 Qt+轮询，不是整应用性能，也不声称已验证 Windows。

| 场景 | Qt 轮询 ms | Qt 通知 ms | 轮询／通知 | 通知／轮询 CPU |
| --- | ---: | ---: | ---: | ---: |
| dmesg-errno-t1 | 22.765 | 8.795 | 2.589 | 0.919 |
| dmesg-errno-t2 | 11.513 | 8.500 | 1.354 | 0.962 |
| dmesg-boolean-t2 | 15.256 | 16.492 | 0.925 | 1.134 |
| dmesg-boolean-inverse-t2 | 18.529 | 18.696 | 0.991 | 1.021 |
| app-multi16-t1 | 200.942 | 201.842 | 0.996 | 1.005 |
| app-multi16-t2 | 162.365 | 163.527 | 0.993 | 1.011 |

主日志 wall 倍率 **0.925～2.589**，通知／轮询进程 CPU 比 **0.919～1.134**；Qt 控制 wall 倍率 **0.974～1.038**、CPU 比 **0.987～1.016**。`dmesg-boolean-t2` 在最终四轮中的三轮较慢，汇总 wall 增加约 **8.1%**、CPU 增加约 **13.4%**；结合首次四轮均较慢，不能认为默认通知没有性能代价。单线程轻查询收益明确，重正则近似持平，但不据此强行全局启用。

边界任务 wall 倍率 **0.348～2.615**，Qt 控制 **0.504～2.691**；这些单块短任务没有容量等待，波动大，不将其倍率归因为通知。

- 最终报告：`out/benchmarks/p12-production-wakeup/results/results.json`，SHA-256 `82322589cead35f23bcde2df538ea9b2bcc879bd1f1af19fc7d46ddc946e8761`。
- 最终 probe：`out/build/p12-utf8-worker/output/zzlogg_search_worker_probe`，SHA-256 `8b0e861e9a80a1e09268b0e63866db3c250dffdb121d859af7c0fdfa4f7940bf`。
- 测量基线 `859d97a71c3a648bb22abb76a23fd22f36c5e887`，实际使用本轮工作树；输入复用 `out/benchmarks/p12-wakeup/input/`。
- 验证日志：`out/benchmarks/p12-production-wakeup/validation/`。

**交付：** 共用门（实验头仅别名）、默认容量通知契约、global_control(2) 的同 worker 回归，以及以上准入记录。生产继续 Qt／原轮询。下一步先定位双线程布尔负载额外 CPU 开销，再决定通知策略是否值得继续优化；不将搜索后端替换混进调度评估。

后续：[调度条件诊断](2026-11-20-search-scheduling-diagnosis.md) 确认 CPU 0／1 是 SMT 同核且 TBB 常规后台名额受限；最终新一轮没有复现该布尔查询退化，但其他负载仍有退化，不将原结果解释为固定锁瓶颈。原始数据保留，默认轮询仍不变。
