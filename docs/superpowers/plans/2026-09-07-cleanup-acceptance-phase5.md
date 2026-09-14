# 清理与验收实施计划（阶段五）

## 当前交付状态（2026-09-14）

Windows 清理代码已合并 master；末行计数和失效搜索结果残留也已修复，旧 XFAIL 已移除。45f7fd2d 更新了 ZzPureTools，2026-09-08 的 Release 编译和 60 项 CTest 均通过。

尚未完成的是旧新版大文件性能对照与 Windows 人工多屏/DPI 验收。Linux/macOS 继续暂停，不作为当前阻塞项。后续进度见 [验收收尾计划](2026-09-14-ui-acceptance-followup.md)。下面的“未合并”和 XFAIL 描述仅是早期历史记录，不是当前状态。

## 最新范围调整

用户已明确暂停跨平台测试，并取消旧名称兼容要求。以下早期勾选项为此前执行记录；当前实现以本节为准。

- 删除 UI2 configure/build/test/workflow 别名及旧构建开关转发。
- 验收目录迁入 `tests/ui_acceptance`，保留已有 `tests/ui` 核心集成测试；CTest 前缀统一为 `zzlogg_ui.*`，同步 CI、夹具文件名和烟测内部标识。
- 不修改第三方 `MUI2.nsh`，不重写历史计划中的旧提交记录，不迁移用户持久化配置。
- 本轮只执行 Windows 构建与测试；Linux/macOS 验收按用户要求暂停，不作为继续工作的阻塞条件。

本次调整已验证：Release 编译成功，统一名称后的 60 项 CTest 通过，Windows 原生应用测试 44 项通过。旧 build preset 调用返回“无此预设”；独立审查发现的指南措辞和禁止旧 GUI 产物检查已修正。历史产物名称只在禁止断言及历史记录中保留，不提供兼容入口。未修改持久化数据格式、未合并推送。

> 使用 writing-plans 细化已批准路线，按 executing-plans 内联执行；验证后逐项中文提交，不等待步骤确认。

**目标：** 收敛构建与测试入口，记录真实平台验收边界，不扩大 UI 或搜索算法改造。

**架构：** 保留 `tests/ui2` 和旧预设作为兼容接口；应用实现仍集中于 `src/ui`。无调用者才删除，不能按名称包含 ui2 批量删除。

**技术栈：** CMake Presets、CMake 脚本测试、Qt 6、Windows MSVC Release。

## 任务 1：补齐 Release UI 预设

文件：`CMakePresets.json`、`tests/ui2/releasepresetcontracttest.cmake`、`tests/ui2/CMakeLists.txt`、`docs/BUILD.md`。

- [x] 测试解析 JSON，断言 windows-vs2026-ui-release 的 build/test/workflow 指向 windows-vs2026-ui 且 configuration 为 Release；当前缺失时失败。
- [x] 添加三个同名入口；测试不筛掉 klogg_smoke，所有注册测试参加验收。不改变旧别名。
- [x] 文档提供 configure、build、test 和 runtime-folder 命令，说明 tests/ui2 是兼容名称，不代表应用还有第二套 UI。
- [x] 使用实际 Release 预设编译并执行全部 CTest，提交。

```powershell
cmake --build --preset windows-vs2026-ui-release --parallel 8
ctest --preset windows-vs2026-ui-release
cmake --build --preset windows-vs2026-ui-release --target zzlogg_runtime_folder
```

## 任务 2：清理核查及平台边界

文件：本计划、`docs/UI_CODE_GUIDE.md`、总路线。

- [x] 用 rg 检查 src/ui2、ZzLoggFluentShell 及装饰回调残留；保留生产烟测协议与测试名称的兼容字符串，不无依据删除。
- [x] Windows 原生应用测试、运行目录检查和独立审查；记录结果并提交。
- [ ] Linux 实机编译及窗口/菜单/输入验证。
- [ ] macOS 实机编译及窗口/菜单/输入验证。
- [ ] 旧版本对照的大文件打开、滚动、搜索、内存占用测试和人工多屏检查。

## 已核实的环境限制

基线 `dbeaab45`，隔离工作区干净。Windows 工具链可用；Ubuntu-26.04 WSL 仅发现 g++，未发现 CMake、Ninja 或 Qt6Widgets pkg-config。没有可用 macOS 主机。本轮不安装操作系统级依赖，不把源码契约测试当作跨平台实机通过；缺失环境的项目保持未勾选。

## 本轮验收结果

Windows Release 新预设实际构建成功，CTest 60/60 通过（41.02 秒）；Windows 原生应用测试 44 项通过，保留既有末行计数 XFAIL。完整运行目录已由测试 fixture 重新生成。独立审查通过。

`git ls-files src/ui2` 无跟踪文件，旧装饰类/安装回调在 src 和 cmake 中无引用；本地仅有空目录，不构成第二套实现。保留烟测协议、CTest 名称、兼容预设，没有无依据删除代码。master 的原有未提交内容未修改。

本阶段仅部分完成；Linux/macOS 实机、人工多屏和旧版大文件性能对照仍未执行。未把环境检查或契约测试视作这些验收通过。保留开发分支，不合并推送。
