# CI 精简实施计划

**授权：** 用户已同意直接精简以节省时间；v26.09.01 重发继续进行。

**目标：** 保留正式版、北京时间零点预览版、更新清单续签、安全扫描和安装专项验收，减少重复构建及 Actions 入口。

**方案：** 5 个工作流：CI Build（PR/手动/复用构建）、Publish（tag/定时/手动发布）、Signed Update Feed、CodeQL（每周/手动）、Update Smoke（相关文件变化/手动）。发布前仍校验固定源提交、完整产物和正式版标签；后续发现旧菜单集成测试假定生产更新源为空，已取消受影响的旧构建，修复后只运行统一 Publish。

- [x] 从 `ci-build.yml` 移除 master push 触发，为 PR 新提交取消过时构建。
- [x] 合并 `release.yml` / `continuous-build.yml` 为 `publish.yml`：push → stable；schedule/dispatch → nightly，后者只接受 master。mode 作为 prepare 输出传给发布步骤，继续使用同一轮运行的产物。
- [x] 删除 `test_env.yml`，移除 CodeQL 的 PR 触发，保留每周与手动扫描。
- [x] 更新清单监听 Publish；旧构建结束后已移除迁移用旧工作流名称。
- [x] 更新文档，执行 actionlint、53 项 Python 发布/签名回归及独立审查，已提交推送并核实普通 master push 未触发全平台构建，GitHub 仅保留 5 个入口。

**取舍：** 普通 master 推送不再立即得到全平台结果，需等待定时发布、打 tag 或手动 CI。PR 仍执行全平台验证。独立 tag 与定时发布若恰好选择同一提交仍会分别构建，本轮不引入跨运行产物复用及其信任边界。
