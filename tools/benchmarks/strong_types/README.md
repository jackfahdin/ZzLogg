# 强类型生成代码与边界探针

这个独立工具用于 P8 的保留／替换评估，不进入应用、默认构建或 CI。`codegen.cpp` 对比真实 `linetypes.h` 与局部简单结构的饱和行号加法、非负差值、普通计数加法；局部结构不是可用的完整替代库。

从仓库根目录运行：

```bash
cmake -S tools/benchmarks/strong_types -B out/benchmarks/p8-strong-types/build \
  -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/gcc_64
cmake --build out/benchmarks/p8-strong-types/build --parallel 4
out/benchmarks/p8-strong-types/build/strong_type_checks
objdump -d out/benchmarks/p8-strong-types/build/CMakeFiles/strong_type_codegen.dir/codegen.cpp.o
```

代码生成比较以 Linux／GCC／GNU objdump 为准，C++17、Release 优化。GCC 关闭函数合并并使用函数分节，保留两版独立函数；逐节原始字节可用 `objcopy --dump-section .text.existing_advance=existing.bin <object>` 导出。不同编译器可能使用不同指令或寄存器，不以字节相同作为通用测试要求。

`strong_type_checks` 输出当前平台的类型大小与对齐。以下在 Linux 独立子进程验证同号溢出的 Release 诊断，禁用 core dump；它预期触发 SIGABRT，不要把它当作正常成功退出的程序：

```bash
python3 - <<'PY'
import resource, signal, subprocess
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
r = subprocess.run(['out/benchmarks/p8-strong-types/build/strong_type_checks', 'overflow'],
                   capture_output=True, text=True)
assert r.returncode == -signal.SIGABRT, (r.returncode, r.stderr)
assert 'conversion would truncate value' in r.stderr
print('Release narrowing precondition verified')
PY
```

探针显式采用生产 Release 的 type_safe 宏：内部 assertions=0、precondition checks=1、arithmetic policy=1、wrapper=1。不将 Release 等同于关闭所有检查；也不声称 narrow_cast 可以完整检查不同符号类型间的转换。默认前置条件失败是终止进程，不是可捕获异常。

本次 GCC 15.2／x86-64 结果：5 种类型均为 8 字节，与底层类型一致；饱和加法两版均为 21 字节且完全相同；差值函数旧版 20 字节、本地版 17 字节，本地版少两条指令；普通计数加法均为 9 字节，区别仅为交换 LEA 的源寄存器。函数长度包含 CET `endbr64`。所有这些函数都没有额外包装调用或动态分配；未进行运行时间测量，不能把生成代码差异换算成搜索提速，也不能推广到 MSVC／ARM。

永久编译期类型约束、Qt 元类型及数值边界测试见 `tests/unit/linetypes_test.cpp`；结论和完整限制见 [P8 记录](../../../docs/superpowers/plans/2026-11-14-strong-types.md)。
