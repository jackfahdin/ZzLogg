# 自动化脚本

- `ci/`：持续集成、打包与发布脚本。
- `dev/`：开发者手动运行的辅助脚本，包括本地更新源、日志生成、性能对比及发布记录统计。
- `quality/`：静态检查工具；`run-clang-tidy.py` 为保留上游逻辑的 LLVM 辅助脚本，需要 Python、PyYAML 和 clang-tidy。

从仓库根目录运行开发脚本，例如：

```sh
python scripts/dev/local_update_feed.py --help
python scripts/quality/run-clang-tidy.py -p <build-directory>
```

本地更新源签名需要 `scripts/ci/requirements-update.txt` 中的依赖。性能对比需要 Windows、Qt 和待测程序；日志生成脚本需要 Bash、Perl，并将大文件写入 `/tmp`。

`dev/benchmark_regex_decode.py` 为 Linux/glibc 下的 Qt 匹配器解码对照入口，支持历史头文件、同源码控制、固定 CPU 和独立分配统计，参数与限制见 [探针说明](../tools/benchmarks/regex_decode/README.md)。

`dev/benchmark_boolean_evaluator.py` 对比两个已构建的布尔求值器探针，采用固定 CPU、交替进程顺序和独立分配统计；复现步骤见 [布尔探针说明](../tools/benchmarks/boolean_evaluator/README.md)。

`dev/benchmark_filewatch.py` 以 ABBA 顺序运行显式文件监控探针，记录空闲进程 CPU、持续追加通知和最终刷新延迟；使用方法与限制见 [文件监控探针](../tools/benchmarks/filewatch/README.md)。

`dev/benchmark_literal_search.py` 固定 CPU 并交替运行纯文本候选与所选参考（默认 Qt）对照，校验完整结果摘要，保存构造／首次／热扫描数据；使用方法与限制见 [纯文本搜索实验](../tools/benchmarks/literal_search/README.md)。

编译型辅助工具放在 [`tools/`](../tools/README.md)，Windows 安装与更新验收脚本放在 [`tests/acceptance/`](../tests/acceptance/)。新增脚本按使用场景放入对应子目录。

`dev/benchmark_search_pipeline.py` 对照生产搜索组件与 memchr 逐行／分批适配，保存 ABBA 结果及独立进程诊断；见 [搜索组件探针](../tools/benchmarks/search_pipeline/README.md)。
