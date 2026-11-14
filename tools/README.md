# 编译型辅助工具

- `gen_big_file.cpp`：大日志文件生成器。
- `icon_generator/`：图标生成程序及其 CMake 检查。
- `update/`：本地更新协议测试用的 `testfeed` 程序。
- [`benchmarks/strong_types/`](benchmarks/strong_types/README.md)：P8 强类型布局、生成代码和窄化前置条件检查，独立构建。
- [`benchmarks/boolean_evaluator/`](benchmarks/boolean_evaluator/README.md)：P6 布尔求值器语法、真值表、创建及热求值对照，需显式构建。
- [`benchmarks/regex_decode/`](benchmarks/regex_decode/README.md)：默认 Qt 匹配器的独立解码性能与分配对照，不进入应用构建。

这里保留需要编译的辅助程序及其构建文件。开发、CI 和静态检查脚本按职责放入 [`scripts/`](../scripts/README.md)；Windows 安装与更新验收脚本放入 [`tests/acceptance/`](../tests/acceptance/)。
