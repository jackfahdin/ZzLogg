# 仓库目录约定

[返回文档目录](../README.md) · [构建指南](BUILD.md)

## 日常入口

| 目录 | 内容 |
| --- | --- |
| `src/` | 应用入口、UI、日志数据、存储和更新模块 |
| `tests/` | 自动化测试、安装验收和固定样本 |
| `tools/` | 需要编译的图标生成器、大文件生成器与更新测试工具 |
| `scripts/ci/` | CI、打包与发布脚本 |
| `scripts/dev/` | 本地更新源、日志生成、性能对比与发布统计脚本 |
| `scripts/quality/` | 静态检查辅助脚本 |
| `packaging/` | 平台安装配置、发布图标和更新公钥 |
| `cmake/` | 构建模块、依赖规则和生成文件模板 |
| `3rdparty/` | 第三方源码、子模块和许可证 |
| `website/` | 网站内容和主题 |
| `.github/` | Actions 工作流、复合动作及历史容器定义 |
| `out/` | 被 Git 忽略的本地构建、缓存和临时输出 |

`docs/user/` 是用户手册，`docs/development/` 是当前开发操作，`docs/architecture/` 是实现结构参考。带日期的计划、实验数据及任务报告保留在 `docs/archive/`；归档说明和复现限制见[历史资料索引](../archive/README.md)。

## 测试组织

`tests/unit/` 和 `tests/ui/` 保留原有单元与集成测试。更新系统分别对应 `tests/update/`、`tests/updateqt/` 和 `tests/updater/`。

`tests/ui_acceptance/` 的文件按 `app/`、`storage/`、`ui/`、`packaging/`、`update/` 分组。其 `cmake/` 片段在同一个目录作用域加载，因此现有目标名称、`zzlogg_ui.*` 测试名称和测试输出目录保持不变。片段顺序包含生成代码与目标间依赖，新增测试时应放入对应分组，不要随意重排 include 顺序。

Windows 安装与更新验收脚本放在 `tests/acceptance/`。固定日志样本放在 `tests/fixtures/logs/`，保留原始字节与编码；生成的大日志放入 `out/` 或临时目录。

## 资源与构建产物

应用运行时的图片、字体和翻译仍放在 `src/app/`。Windows/macOS 发布图标位于 `packaging/assets/`，平台打包定义分别位于 `packaging/linux/`、`packaging/windows/`、`packaging/macos/`。

常规构建优先使用 `CMakePresets.json` 定义的目录：普通构建在 `out/build/<preset>/`，UI 测试构建在 `out/ui-ninja/<preset>/`。试验构建也放在 `out/build/` 下，并使用能识别用途的名称。

CMake 缓存包含绝对路径，不能通过直接移动构建目录来复用。旧目录失效时删除并重新配置；保留仍在使用的构建、SDK 和依赖下载缓存。安装包通常由构建目录下的 `packages/` 生成，不应提交到源码树。

## 路径迁移

| 原位置 | 当前位置 |
| --- | --- |
| `Resources/` | `packaging/assets/` |
| `packaging/osx/` | `packaging/macos/` |
| `test_data/` | `tests/fixtures/logs/` |
| `tools/acceptance/` | `tests/acceptance/` |
| `scripts/local_update_feed.py` | `scripts/dev/local_update_feed.py` |
| `tools/compare-ui-performance.ps1` | `scripts/dev/compare-ui-performance.ps1` |
| `tools/run-clang-tidy.py` | `scripts/quality/run-clang-tidy.py` |
| `docker/` | `.github/containers/` |

源码中的 `update`、`updateqt` 和 `updater` 分别承担协议核心、Qt 服务、独立更新执行职责；它们有不同的依赖和 Windows 运行库约束，不能仅因名称相近而合并。
