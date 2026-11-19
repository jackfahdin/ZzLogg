# PCRE2-8 直接 UTF-8 正则实验

这是独立组件实验，不被顶层 CMake 引用，不进入应用／all／ci_build，不新增生产依赖。Qt 内部是非公开的 PCRE2-16，不能直接当作 PCRE2-8 使用；本工具显式从本机 Qt 配套的 PCRE2 **10.47** 源码另编 8-bit 静态库。不自动下载、不复制 vendor、不修改 Qt 安装目录；系统 PCRE2 10.46 不参与本次比较。源码许可证见供应目录的 `LICENCE.md`、`LICENSE-SLJIT`（BSD-3-Clause 及其 binary-like exception），本工具不分发该源码或构建产物。

```bash
cmake -S tools/benchmarks/utf8_regex -B out/build/p12-utf8-regex -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/gcc_64 \
  -DPCRE2_QT_SOURCE_DIR=/path/to/Qt/6.11.1/Src/qtbase/src/3rdparty/pcre2 \
  -DMIMALLOC_LIBRARY=/absolute/path/to/ZzLogg/out/build/p6-boolean/output/libmimalloc.a
cmake --build out/build/p12-utf8-regex --parallel 4
out/build/p12-utf8-regex/utf8_regex_probe --verify
python3 scripts/dev/benchmark_utf8_regex.py \
  --binary out/build/p12-utf8-regex/utf8_regex_probe \
  --corpus out/benchmarks/p11-android/corpus-verified \
  --output out/benchmarks/p12-utf8/results --rounds 4
```

语料可由 [Android 生成器](../search_worker/README.md#android-驱动开发模拟语料)生成。runner 固定副本、四轮交替进程与 ABBA／BAAB 顺序，同时运行 Qt/Qt 控制；Linux 固定前两个可用 CPU。输出必须为空目录，出错不发布成功汇总。每次验证固定查询、完整逐行逐模式位图、原始文件摘要、实际回退和 JIT 程序数。Python 只对这一组固定语法和单个 FF 坏字节作独立预期，不是通用 PCRE/Qt 参考实现。

## 语义与生命周期

编译采用 UTF／UCP／NO_AUTO_CAPTURE 以及既有大小写选项；Qt 原模式先校验并保留作回退。PCRE2_NEVER_BACKSLASH_C 拒绝代码单元相关的 `\C`，这些模式交 Qt 处理，普通字面量 `\\C` 仍可直接匹配。每行先验证 UTF-8；坏编码或行首 BOM 整行回 Qt，保留 Qt 的替换／去 BOM 语义。PCRE 编译不支持或运行错误也回 Qt，运行错误不能偷偷当成无命中。

候选自己持有编译结果、match data、match context 和最多 512 KiB 的 JIT stack（初始 32 KiB），RAII 释放。按 matcher 使用独立实例；同一实例即使通过 const 方法也不能并发扫描。申请 JIT 后查询实际代码大小，允许单个模式解释执行；本轮契约和跑分要求环境有实际 JIT，固定跑分模式必须全部产生 JIT 代码，防止空跑。

`firstMatch()` 仅作首个完整匹配的 UTF-8 字节→UTF-16 坐标验证，不是全局高亮迭代。测试涵盖 Unicode／emoji、内嵌 BOM、环视、命名反向引用、空匹配、锚点、CRLF、NUL、坏字节、切片、空模式列表、长行、512 个随机字节输入、move／销毁，以及运行限制触发 Qt 回退。生产高亮和加速路由未改。

## 计时及资源范围

8 个场景包含 Android 单正则、16 条规则、忽略大小写和异常边界。Qt 参考直接调用实际 `DefaultRegularExpressionMatcher`。输入与结果参考在计时前准备；分别记录 matcher 构造、第一次全量扫描、两者总和和热扫描。Qt 惰性编译发生在首次扫描，候选构造时编译并保留 Qt＋PCRE2，因此必须看构造加首次扫描的总和，不能只比较不对称的构造时间。

热扫描包括 UTF-8 校验、按需解码、匹配及位图收集；不含 I/O、索引、worker 调度／取消、GUI。输入一次读入内存，适合本轮约 25 MB 语料，不作为多 GB 内存实验。首次扫描不是冷进程启动，JIT 代码尺寸也不等于峰值／常驻内存；Qt 回退缓存、match data、stack 和文件缓存均需另算。匹配坐标转换不在位图扫描计时中。

`QT_ENABLE_REGEXP_JIT=1` 请求 Qt JIT，但不能经公开 Qt API确认每个模式的实际 JIT 状态；候选用 PCRE2_INFO_JITSIZE 直接记录。报告保存二进制、工具源码、外部 PCRE 源码摘要、CMakeCache、版本、路由和计时。正式计时期间不要运行构建、测试或调试器。

GCC/Clang 可用 `-DUTF8_REGEX_SANITIZE=ON` 在单独构建目录启用 Address/Undefined Sanitizer 并执行 `--verify`；候选和 PCRE C 源码被插桩，预编译 Qt／mimalloc 与 JIT 生成机器码不属于同等插桩范围。结果及是否采用见 [P12 记录](../../../docs/superpowers/plans/2026-11-19-utf8-regex-probe.md)。
