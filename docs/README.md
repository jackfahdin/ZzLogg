# ZzLogg 文档

[返回项目首页](../README.md)

日常使用从用户手册开始；构建、测试和发布从开发指南开始。架构参考介绍实现结构，并链接相关设计依据。历史资料保留阶段背景、决策过程和原始验证证据。

| 目录 | 入口与用途 |
| --- | --- |
| `user/` | [简体中文用户手册](user/DOCUMENTATION.md)、[English](user/i18n/en/DOCUMENTATION.md)、[繁體中文](user/i18n/zh_TW/DOCUMENTATION.md)；三语手册同时供应用内帮助使用 |
| `development/` | [仓库目录约定](development/REPOSITORY_LAYOUT.md)、[构建指南](development/BUILD.md)、[CI 与发布](development/CI_RELEASE.md)、[版本约定](development/VERSIONING.md)、[CI 已知失败](development/CI_KNOWN_FAILURES.md) |
| `architecture/` | [架构参考索引](architecture/README.md)、[UI 代码学习指南](architecture/UI_CODE_GUIDE.md)、[代码文本高亮](architecture/code-syntax-highlighting.md) |
| `archive/` | [历史资料索引](archive/README.md)：设计与实现计划、性能实验、工具任务报告 |

## 更新系统开发

- [GitHub 更新源](development/GITHUB_UPDATES.md)：签名清单与发布流程。
- [更新协议、检查与下载](development/UPDATE_PROTOCOL.md)：协议约束、实现入口与各阶段范围。
- [Windows VM 验收手册](development/UPDATE_ACCEPTANCE_VM.md)：真实安装与更新环境的操作要求。
- [阶段 4 验收分诊](development/UPDATE_STAGE4_TRIAGE.md)：结果分类与排查入口。

## 资料时效

当前开发操作优先参考 `development/`，界面行为优先参考 `user/` 和当前源码。`archive/` 按原日期保留资料；归档不表示其中所有设计已失效。仍适用的设计依据由[架构参考索引](architecture/README.md)和相关开发指南链接，具体适用范围以文中的后续修订及当前实现为准。

历史命令、代码片段、绝对路径、构建目录、测试数量与性能结果是当时的快照，不代表本次重新执行。复现前应根据当前构建指南与目录结构核对环境；原始日志、补丁和数据保持原样。
