# 历史资料

[返回文档目录](../README.md) · [架构参考](../architecture/README.md) · [当前构建指南](../development/BUILD.md)

这里保存设计与实现过程、阶段验证和原始实验资料。归档表示按时间保留上下文，不表示所有设计约束都已失效；仍被当前实现采用的设计可继续作为参考。现行操作入口见开发指南，设计演进见架构参考。

| 目录 | 内容 |
| --- | --- |
| [plans/](plans/) | 单文件关闭、统一存储等早期设计说明 |
| [superpowers/specs/](superpowers/specs/) | 带日期的设计规格及范围修订 |
| [superpowers/plans/](superpowers/plans/) | 实现步骤、阶段状态和验证记录 |
| [performance/](performance/) | 性能报告、原始 CSV/JSON、运行日志与实验补丁；入口为 [2026-09-14 性能记录](performance/2026-09-14/README.md) |
| [task-reports/](task-reports/) | 原 `.superpowers/sdd/` 中已入库的[行号配置任务报告](task-reports/2026-09-03-runtime-language-and-line-number-unification/task-1-report.md) |

文中的命令、代码片段、旧目录名称、机器绝对路径和测试结果保留当时快照，不能直接视为当前可执行操作或新的验证结论。文档之间及指向现有源码的 Markdown 链接随目录迁移修正；原始补丁、日志和实验数据不改写。复现实验时应先核对记录对应的提交、平台、依赖和当前目录映射。
