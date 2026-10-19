# 架构参考

[返回文档目录](../README.md)

| 当前参考 | 内容 |
| --- | --- |
| [UI 代码学习指南](UI_CODE_GUIDE.md) | 应用入口、窗口与文档生命周期、日志视图、主题、多语言及测试入口 |
| [代码文本高亮](code-syntax-highlighting.md) | 语法状态、缓存边界、资源与构建依赖 |
| [更新协议、检查与下载](../development/UPDATE_PROTOCOL.md) | 更新核心、信任边界、检查、下载与安装交接 |

## 设计依据与演进

以下设计按原日期保存在历史目录，便于结合实现计划和验证记录追溯。它们仍可用于理解相关架构约束；阶段范围、接口和命令须结合后续修订及当前源码阅读。

- [统一存储设计](../archive/plans/2026-09-01-unified-storage-design.md)：存储根目录、配置与会话布局、迁移约束。
- [单文件关闭设计](../archive/plans/2026-09-01-single-file-close-design.md)：标签关闭与文档生命周期。
- [UI 演进路线](../archive/superpowers/plans/2026-09-07-zzpuretools-ui-roadmap.md)：窗口、工作区、日志页面和设置职责的拆分背景。
- [运行时语言与行号统一](../archive/superpowers/specs/2026-09-03-runtime-language-and-line-number-unification-design.md)：动态语言切换与行号配置的设计依据。
- [仅安装版在线更新设计](../archive/superpowers/specs/2026-09-15-installer-only-update-design.md)：安装版与便携版的更新范围。
- [安装事务设计](../archive/superpowers/specs/2026-09-17-install-transaction-design.md)：安装交接、失败恢复与事务约束。
- [Windows 一键更新设计](../archive/superpowers/specs/2026-09-21-windows-one-click-update-design.md)：签名清单锚定的一键更新流程。

更多实现计划和实验结果见[历史资料索引](../archive/README.md)。
