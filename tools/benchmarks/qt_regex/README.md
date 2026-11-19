# Qt 正则预筛选复核对照

P12 的显式组件工具，链接实际 `klogg_regex`。只在 `ZZLOGG_BUILD_QT_REGEX_PROBE=ON` 时声明目标，仍排除在 all／ci_build 之外，无新依赖。预筛选模式要求实际 `HsPrefilterMatcher`，没有加速库或静默回退会失败；Qt 模式用 `DefaultRegularExpressionMatcher` 作前后同实现控制。

```bash
cmake -S . -B out/build/linux-gcc-vectorscan-release -DZZLOGG_BUILD_QT_REGEX_PROBE=ON
cmake --build out/build/linux-gcc-vectorscan-release --target zzlogg_qt_regex_probe --parallel 4
# 修改生产代码前保存一份同工具源码构建的 baseline 二进制，改后再构建 current。
python3 scripts/dev/benchmark_qt_regex.py \
  --before /absolute/path/baseline/zzlogg_qt_regex_probe \
  --after out/build/linux-gcc-vectorscan-release/output/zzlogg_qt_regex_probe \
  --corpus out/benchmarks/p11-android/corpus-verified \
  --output out/benchmarks/p12-qt-reuse/results --rounds 4 --jit 1
```

可用 [Android 生成器](../search_worker/README.md#android-驱动开发模拟语料) 准备三个 UTF-8 文件。runner 复制本次输入到临时目录，各进程只读取固定副本；输出必须为空目录。固定查询是 dmesg 的 I2C 超时正向环视、logcat 的 binder 排除 debug、应用 JSON 中 IOException／SIGSEGV／ANR 的后向环视。Python 独立核对每个物理行／模式的完整位图、命中数、模式与输入摘要；不作为通用 Qt 正则参考。主日志应停止写入，本工具只针对这一组 UTF-8 实验。

每轮前后二进制采用 ABBA／BAAB 顺序，共四轮，每侧每场景八个进程样本；Linux 固定前两个可用 CPU。分别报告 matcher 设置（加速模式含数据库构建）、首行、第一次全量扫描和热扫描。首行未命中预筛选时不触发 Qt 惰性编译；首次全量扫描才覆盖实际命中的模式。Qt 参考和所有输入在计时前准备；这些是组件时间，包含解码、匹配、位图收集，不包括文件 I/O、索引、worker 调度／取消、高亮或 UI。文件全部读入内存，不用于多 GB 测量。构建时间不等于编译器／应用干净构建时间。

`--jit 1/0` 显式设置 Qt 的 JIT 请求开关，不保证每个表达式／平台都能产生 JIT 代码。正式计时期间不运行构建、测试或调试器。每个进程原始输出和最终汇总都保留；失败不生成成功汇总。

## 复用诊断

```bash
gdb -q -batch -x tools/benchmarks/qt_regex/verify_reuse.gdb \
  --args out/build/linux-gcc-vectorscan-release/output/zzlogg_qt_regex_probe --reuse
```

这是本地可选 GDB 诊断，依赖 Qt 的公开 C++ 构造符号及本工具的两处边界函数，不进入默认 CI。使用实际预筛选、真实 Qt 和固定命中位图，预热后扫描 64×4 行，观察从 QString 构造 Qt 正则的次数：旧实现 256，新实现应为 0。必须先在预热阶段实际观察到构造，避免符号缺失时把未观测误认为零开销；边界未执行／进程失败／热路径次数非零均失败。只证明不重复构建模式，不声称整个扫描没有分配或所有 JIT 编译都成功。

生产测试在 `zzlogg_search_backend_test` 中覆盖环视假阳性、中文、NUL、大小写、多模式、空行、重复调用和源表达式销毁后的两个独立 matcher。详见 [审计及结果](../../../docs/superpowers/plans/2026-11-19-qt-regex-reuse.md)。
