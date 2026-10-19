# 仓库目录整理计划

目标：减少根目录噪音，区分当前文档、历史资料、自动化脚本、编译工具和测试样本；保持源码模块、可执行程序及 CTest 名称稳定。

## 迁移与验证

- [x] 清理失效的 `out/build/ci-qt6112` 构建缓存和已确认为空的目录，保留当前有效构建、SDK 与下载缓存。
- [x] 文档分为 `docs/user`、`docs/development`、`docs/architecture` 和 `docs/archive`；迁移历史计划、性能实验和已入库的工具任务报告；添加 `docs/README.md`，修正链接。
- [x] `scripts/dev` 放开发脚本，`scripts/quality` 放静态检查脚本，`tests/acceptance` 放安装验收脚本，`tools` 保留编译工具；同步所有执行入口。
- [x] `tests/ui_acceptance` 内按 app、storage、ui、packaging 分组文件与 CMake 定义，保持测试名称、输出路径和执行语义。
- [x] `Resources` 迁至 `packaging/assets`，`packaging/osx` 统一为 `packaging/macos`，`test_data` 迁至 `tests/fixtures/logs`，容器定义迁至 `.github/containers`；更新引用与忽略规则。
- [x] 扫描旧路径和文档链接；配置 CMake、构建受影响目标、运行对应 CTest 和 Python CI 测试；独立审查变更并处理发现。

源码模块和第三方依赖保持原结构。当前发布标签不随目录整理移动。新增固定测试样本应跟踪入库，生成的大日志应放在 `out`。本计划完成后作为本次迁移记录归档。

## 验证结果

- 290 个受跟踪文件迁移均有路径映射；未丢失文件，固定日志样本和历史非 Markdown 附件字节保持一致。
- 84 个 CTest 名称、命令和属性在路径归一化后与整理前一致。
- Python CI 测试：80 个通过。
- Linux Release 的应用程序及除更新集成测试外的全部 `ci_build` 目标构建成功。
- CTest：82 个通过，`zzlogg_update.policy` 失败，`zzlogg_update.integration` 因未能链接而未运行。两项已通过基线核验记录在当前开发文档的 CI 已知失败清单中；没有新增 CI 排除规则。
- 当前文档本地链接检查、迁移引用扫描和独立代码审查通过；审查发现的 Windows Inno Setup 反斜杠图标路径已修正。
- Windows/macOS 原生打包未在本机执行。发布标签未移动，远程仓库未推送本次目录改动。

本记录对应 2026-10-19 的目录迁移；后续新增文件应遵循当前 `docs/development/REPOSITORY_LAYOUT.md`。
