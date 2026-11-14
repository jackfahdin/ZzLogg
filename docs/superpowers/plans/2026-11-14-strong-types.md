# P8：type_safe 兼容性与维护成本评估

**基线：** `5b84229`。沿用已确认路线：收益不足时保留依赖，不以删除库为目标。

**目标：** 明确 5 个强类型和窄化操作的真实保障，验证替代方案是否值得维护。

**候选方案：** 保留现有 type_safe 并补齐约束测试；自写 5 个独立结构（操作符重复）；或自写标记类型模板与受检转换（需维护一套公共基础设施）。C++20 的 `std::in_range` 只能判断整数是否可表示，不能替代强类型、操作符集合或错误处理策略；核心当前仍以 C++17 构建。优先比较现有实现与小型本地结构的生成代码，再判断收益。

## 执行

- [x] 审计 OffsetInFile、LinesCount、LineNumber、LineLength、LineColumn：底层类型、默认值、运算符集合、饱和运算、Qt 元类型、调试输出与可选值。
- [x] 增加编译期约束及边界回归：禁止隐式整数／跨单位转换和无意义混算，保留有意定义的单位运算；测试窄化的有效边界，不依赖未定义行为。
- [x] 比较优化后机器码及本机布局，明确性能结论范围；对 Release 的窄化失败行为用独立进程探针验证。
- [x] 评估源码清理量与自维护职责；有充分收益才替换，否则记录保留原因。
- [x] 完整构建、CTest、Python CI，独立审查，更新总路线与中文提交，不推送。

## 已发现的边界

- `strong_typedef` 的基础加减／递增直接使用底层整数运算；饱和语义来自项目 `boundedAdd`／`boundedSubtract`，不是 type_safe 的一般保证。
- `FilePosition` 默认列为 -1，必须保留这个哨兵；不能无条件把列类型改为无符号。
- Release 默认关闭内部 assertions，但 precondition checks 仍开启，不能将 narrow_cast 当作纯 static_cast。
- 当前 narrow_cast 实现按源／目标宽度判断范围，不是覆盖所有符号组合的完整 checked cast；使用方需要满足可表示前提。

## 结果

**结论：完成评估，保留 type_safe，不实施替换。** 当前库只提供头文件编译依赖，没有独立运行库；Git 跟踪 44 个文件，共 456712 字节（约 446 KiB）。移除它的主要收益是源码量和依赖数量，尚不足以抵消自维护类型契约及转换策略的成本。

### 类型契约

| 类型 | 底层类型 | 允许的关键操作 |
| --- | --- | --- |
| OffsetInFile | int64_t | 同类型加减、递增、比较；允许负值 |
| LinesCount | uint64_t | 同类型加减、递增递减、比较；无符号基础运算按整数规则回绕 |
| LineNumber | uint64_t | 递增递减、比较；加减 LinesCount，两个行号之差为 LinesCount |
| LineLength | QString::size() 的类型（Qt 6 为 qsizetype） | 同类型加减和比较；签名是有符号类型 |
| LineColumn | 同上 | 递增、比较；加减 LineLength，两个列位置之差为 LineLength；-1 是默认 FilePosition 的无效列哨兵 |

所有类型默认值为 0，整数构造显式，五种单位之间不能直接构造、赋值或隐式转换。仅特定单位组合可混合加减；行号 + 行号、列位置 + 列位置、单位 + 裸整数不开放。LineNumber 与 LinesCount 额外允许 `<` 和 `>=` 比较。LinesCount、LineNumber、LineLength、LineColumn 已声明 Qt 元类型，QDebug 输出底层数值；OptionalLineNumber 的空值和有效 0 行不能混淆。

饱和行为来自项目自己的操作符：非负位置加数量在上限饱和、减去更大数量返回 0。该实现以增量非负且中间有符号差值可表示为前提，不是任意有符号数的通用饱和运算；不能将 LineColumn 的 -1 哨兵随意代入所有边界计算。type_safe 的基础加减和递增递减沿用整数规则，不提供普遍溢出保护。

窄化调用共 27 处，主要是指针距离／块内偏移／Qt 字符长度转目标整数类型。Release 的 `TYPE_SAFE_ENABLE_ASSERTIONS=0` 与 `TYPE_SAFE_ENABLE_PRECONDITION_CHECKS=1` 是两项独立配置。独立进程验证 int64_t 中的 INT_MAX+1 转 int 会给出 `conversion would truncate value` 并以 SIGABRT 退出；不会抛出可捕获异常。

当前 narrow_cast 的范围检测主要针对变窄的同号类型。等宽不同符号等情况不获得完整检查保障；模板参数可用不代表所有值都合法。评估中没有发现据此扩大公开输入范围或改变现有失败策略的理由。`std::in_range` 可辅助将来的转换策略设计，但仍需定义失败行为、支持核心 C++17，且不能代替强类型与操作符。

### 替代成本与生成代码

保留方案维持一个固定版本头文件库。五个手写结构需要重复显式构造、比较与可选操作符；通用标记模板则重新引入 mixin／约束、底层值访问、constexpr、Qt 日志适配和窄化实现。两条替换路线都需要跨 GCC、Clang、MSVC 验证，不能以 `using X = uint64_t` 保留单位保护。

本机 Linux x86-64／GCC 15.2／Qt 6.11.1／C++17 Release 的小型对照结果：

| 检查 | 现有实现 | 局部简单结构 | 解释 |
| --- | --- | --- | --- |
| 五种类型布局 | 均为 8 字节／8 字节对齐 | 底层类型同为 8 字节 | 本机没有额外存储开销 |
| 行号饱和加法 | 21 字节 | 21 字节 | 完全相同的机器码 |
| 行号非负差值 | 20 字节 | 17 字节 | 本地版少两条指令；未测运行耗时 |
| 普通计数加法 | 9 字节 | 9 字节 | 同样为 LEA，源寄存器顺序不同 |

函数长度含 CET 入口指令。三组算术均内联为整数指令，无包装函数调用或动态分配。这不是完整替代库，也不是应用基准；没有衡量编译耗时、搜索吞吐或 UI 响应，不声称所有生成代码相同、全部平台零开销或替换无价值。现有证据不足以支持运行性能收益。

复现工具为 [strong_types 探针](../../../tools/benchmarks/strong_types/README.md)，仅手动独立构建。原始布局、Release 失败诊断与字节对照在本地忽略目录 `out/benchmarks/p8-strong-types/result.json`，反汇编在同目录 `codegen.txt`。

### 验证范围

新增 `tests/unit/linetypes_test.cpp`：编译期检查全部 5×5 单位组合的转换／赋值／比较与加减约束，检查有意义操作的返回单位；运行时检查默认值、复制、比较、QDebug、四种 Qt 元类型、可选值、-1 哨兵、非负位置的饱和边界、基础整数运算、递增递减和合法窄化边界。

生产类型、依赖清单和转换策略均保持原状。Linux／Qt 6.11.1 下 `ci_build` 成功，96 项 CTest 全部通过（13.27 秒）；其中 `klogg_tests` 为 40084 条断言／17 个用例，新增强类型部分为 107 条运行时断言／3 个用例，另有编译期断言。Python CI 124 项完成，3 项跳过，无失败。独立 Release 探针的布局、生成代码和窄化失败诊断验证通过。

```bash
cmake --build out/build/p6-boolean --target ci_build --parallel 12
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/p6-boolean --output-on-failure --parallel 8
python3 -m unittest discover -s tests/ci
```

Windows／macOS 原生执行仍由 CI 完成；尤其不将 GCC 的布局／指令结果当作 MSVC／ARM 的实测结论。

独立审查已核对测试语义、平台边界及原始生成代码证据，未发现阻塞或重要问题。本轮以中文提交，不推送。
