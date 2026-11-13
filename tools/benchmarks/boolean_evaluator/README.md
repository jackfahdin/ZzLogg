# 布尔求值器兼容性与性能对照

探针通过 `klogg_regex` 调用真实 `BooleanExpressionEvaluator`，不复制求值器，也不计入应用或 `ci_build` 的默认目标。启用 `KLOGG_BUILD_TESTS` 后显式构建：

```bash
QT_ROOT=/path/to/Qt/6.11.1/gcc_64 cmake --preset linux-gcc-release \
  -B out/build/p6-boolean -DKLOGG_BUILD_TESTS=ON -DWARNINGS_AS_ERRORS=ON
cmake --build out/build/p6-boolean --target zzlogg_boolean_probe --parallel 4
mkdir -p out/benchmarks/p6-boolean
out/build/p6-boolean/output/zzlogg_boolean_probe --cases out/benchmarks/p6-boolean/current.json
```

`--cases` 生成操作符、优先级、结合性、括号、非法输入、固定随机表达式的 410 个样例及所有输入真值。它直接调用求值器，包含公开搜索入口不接受的算术和空白写法。永久回归数据位于 `tests/unit/data/boolean-compatibility.json`：旧实现基线为 `54407c3`，移除了耗时字段，并用 `public_grammar` 标记公开语法。不要用新实现覆盖旧真值表。

## 重建旧版本探针

在独立目录检出基线，只附加相同探针及其 CMake 入口。不要将新的生产代码或单元测试复制进旧树。以下从当前仓库根目录执行；目录必须尚不存在，两个构建应使用同一编译器、Qt、Release 配置和后端选项：

```bash
git worktree add --detach out/benchmarks/p6-baseline 54407c3
git -C out/benchmarks/p6-baseline submodule update --init --recursive
cp -R tools/benchmarks/boolean_evaluator out/benchmarks/p6-baseline/tools/benchmarks/
cat >> out/benchmarks/p6-baseline/tests/unit/CMakeLists.txt <<'CMAKE'

add_subdirectory(${PROJECT_SOURCE_DIR}/tools/benchmarks/boolean_evaluator
                 ${CMAKE_CURRENT_BINARY_DIR}/boolean_probe EXCLUDE_FROM_ALL)
CMAKE
cd out/benchmarks/p6-baseline
QT_ROOT=/path/to/Qt/6.11.1/gcc_64 cmake --preset linux-gcc-release \
  -B out/build/boolean-probe -DKLOGG_BUILD_TESTS=ON -DWARNINGS_AS_ERRORS=ON
cmake --build out/build/boolean-probe --target zzlogg_boolean_probe --parallel 4
cd ../../..
```

本次测量的旧二进制在修改生产代码前已构建并保存为 `out/benchmarks/p6-boolean/baseline-probe`；以上步骤可重新生成等价的旧代码探针，二进制哈希可能因路径、编译器等因素不同。

## 计时与分配

runner 使用 Linux CPU affinity；可选分配插桩限 glibc。探针的纯计时不要求 glibc 插桩。分配库可直接从 P5 探针源码构建：

```bash
cc -shared -fPIC -O2 tools/benchmarks/regex_decode/alloc_probe.c \
  -o out/benchmarks/p6-boolean/libregex_alloc_probe.so
python3 scripts/dev/benchmark_boolean_evaluator.py \
  --baseline-binary out/benchmarks/p6-baseline/out/build/boolean-probe/output/zzlogg_boolean_probe \
  --current-binary out/build/p6-boolean/output/zzlogg_boolean_probe \
  --allocation-probe out/benchmarks/p6-boolean/libregex_alloc_probe.so \
  --output out/benchmarks/p6-boolean/results --rounds 8
```

每个探针进程分别测量 1、3、4、6、16、64 个模式下的 `and`、`&`、`or`、`|` 链。每项构造 32 次取平均（包括析构），预热后对 32 组固定输入执行 100000 次求值；全真、全假和周期性混合输入均有覆盖。求值结果先与 `all_of`／`any_of` 独立校验，再校验新旧 checksum。单变量时四种操作符名称对应相同表达式，可用于观察启动和测量顺序的影响。

默认 8 轮 ABBA／BAAB 进程顺序，每版 16 个样本，固定在首个允许的 CPU，未锁定 CPU 频率。计时进程移除 `LD_PRELOAD`；分配计数在另一次进程运行中进行，要求两版每项热求值观察到零次 glibc malloc/calloc/realloc 请求。它不统计 mimalloc、mmap、RSS 或峰值内存；生产求值循环无动态分配也通过代码检查确认。runner 保存逐次数据、中位数、CPU 编号和二进制 SHA-256。

这是布尔组件基准：不含正则匹配、文件读取、UI 或整应用搜索耗时。短路只跳过布尔求值指令；各子模式的匹配仍由现有匹配器完成。本次未单独计时所有混合操作符、极深表达式，也未运行相同源码双二进制控制，几纳秒级差异不作为普遍提速或无退化证明。结果、边界和验证记录见 [P6 实施记录](../../../docs/superpowers/plans/2026-11-13-boolean-evaluator.md)。
