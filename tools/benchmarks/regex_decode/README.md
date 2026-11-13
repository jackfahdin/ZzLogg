# 默认 Qt 匹配器的解码对照

从仓库根目录运行，先用常规项目构建生成 mimalloc 静态库：

```bash
python3 scripts/dev/benchmark_regex_decode.py \
  --qt-root /path/to/Qt/6.11.1/gcc_64 \
  --mimalloc-library out/build/p4-throttler/output/libmimalloc.a \
  --baseline 302a354 --rounds 8 --require-savings
```

此工具独立构建，不进入应用或 CI 的默认构建图。runner 的分配测量目前限 Linux/glibc；`main.cpp` 的无插桩计时可单独构建运行。需要 CMake、Ninja、C/C++ 编译器、Qt Core 和与编译器兼容的 mimalloc 静态库。

旧版本仅从 Git 提取 `hsregularexpression.h`，其他头文件及依赖共用当前工作树，因此适用于 P5 这种局部改动；不是任意历史版本的完整应用对照。两个真实头文件分别编译，重命名类型以避免 ODR 冲突，通过相同的虚函数边界在一个进程中调用；不改动生产接口。提取后的头文件、构建日志、环境信息、输入/结果 SHA-256、逐轮数据和中位数摘要写入 `out/benchmarks/p5-regex-decode/`。`--output` 可指定其他输出目录。

每个场景生成 128 行固定样本，按 0/1/4/16 个正则模式进行逐行对照：256 字节和 8192 字节的 ASCII/中文、非法 UTF-8、嵌入 NUL、空行。模式为 `\bneedle0\b` 至 `\bneedle15\b`；行尾为 `needle<行号 % 16>`，分别形成低命中、混合命中和全命中场景。原始输入及结果摘要必须相同。两版先完成逐模式参考结果对照及预热，再按预热耗时选择 4–65536 遍、目标约 30 ms 的采样长度；复用同一对实例，以 ABBA 顺序计时，每轮交换构造顺序。默认 8 轮、每版 16 次计时，记录 ns/行。所有测量进程固定在同一 CPU（默认第一个允许的 CPU，可用 `--cpu` 指定），但未锁定频率。没有磁盘读取成本，查询构造和首次编译不在计时内，也不代表整应用搜索速度或取消响应。

计时进程不加载分配插桩。另用 `LD_PRELOAD` 探针扫描一遍，统计 glibc 的 malloc/calloc/realloc 请求次数与累计请求字节；不包含 mimalloc 独立分配、mmap、峰值驻留内存或存活内存，realloc 的申请量按完整请求计数。Qt 的 QString 数据分配位于这一范围内。`--require-savings` 要求每个非空多模式场景的分配次数下降，且零／单模式分配次数不变，可在优化前得到失败、优化后得到通过；不使用易波动的耗时阈值作为 CI 测试。

相同源码控制：在上述命令中去掉 `--require-savings`，增加 `--current-ref 302a354 --output out/benchmarks/p5-regex-decode-control`。比较两份完全相同的旧头文件，可发现构造顺序、内存布局、JIT 和系统噪声造成的偏差；若某场景的控制偏差明显，不将其计时差解释为生产优化收益或退化。负对照本身也不能证明不存在性能退化。
