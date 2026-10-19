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

编译型辅助工具放在 [`tools/`](../tools/README.md)，Windows 安装与更新验收脚本放在 [`tests/acceptance/`](../tests/acceptance/)。新增脚本按使用场景放入对应子目录。
