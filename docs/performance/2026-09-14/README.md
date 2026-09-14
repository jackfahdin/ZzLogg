# Windows UI 大日志对照记录

## 结论

本轮完成了指定场景的旧新版对照，但不宣称全面性能无退化。打开耗时接近；搜索第一组有较大波动，第二组基本一致，不能声称新版搜索加速。新版离散滚动重绘两组均略慢，是值得后续剖析的信号：100 次总耗时分别增加 19 ms、11 ms（约 7.6%、4.5%），不是 FPS 或实际连续滚动流畅度指标。峰值工作集两组变化方向不同，未显示稳定增长趋势。

没有根据这些结果修改生产代码，也没有把性能测量完成等同于性能验收全部通过。

## 环境与版本

- Windows 11 家庭版，10.0.26200；AMD Ryzen 9 9950X3D；物理内存 66,191,847,424 字节。
- Qt 6.11.0 msvc2022_64；Visual Studio 18 2026 x64；Release，`/MD /O2 /Ob2 /DNDEBUG`，LTO OFF、Hyperscan OFF。
- 旧版：UI 重构前 `248fc7c58186a55e13f0002804106507f075c24f`，框架 `f9e6c6f25af8061666cc04da2a43c0a1d3cfe271`。
- 新版：生产代码 `45f7fd2d`，框架 `5a3ea3ccb984d1edcf67f088d4c7aa949848d9a1`；测量夹具见 `9265f5b2`。构建发生在夹具提交前，二进制以 manifest 中 SHA-256 为准。
- 旧版只追加测试适配，不修改生产代码；[适配补丁](baseline-adapter.patch) 保留旧框架标题栏和全局样式初始化。两边框架不同，因此不能将差异单独归因于 UI 重构。

## 测量方法

两组独立执行，每组两版本各热身一次，再各测五次，交替执行顺序；没有并行构建或基准测试。每次启动新进程与临时配置，生成完全相同的 138,420,224 字节（约 132 MiB）、2,097,152 行日志，区分大小写普通搜索 ERROR，严格断言 2,048 条匹配。

窗口 1280×800，深色英文；全部日志确认主视图 viewport 为 1239×539，DPR 为 1.00。开始前等待窗口曝光。搜索完成后，滚动条按最大值的 1% 到 100% 设置 100 次，每次同步 repaint 并处理事件；实际 Paint 事件为 100–101 次。

打开和搜索计时包含 QTRY 约 50 ms 的轮询粒度，不能用于解释小幅百分比变化。日志刚生成，属于热文件缓存，不是冷盘读取。滚动是离散位置跳转和绘制处理耗时，不包含真实鼠标轨迹或帧呈现统计。

峰值为 `K32GetProcessMemoryInfo` 的进程 PeakWorkingSetSize，取样至滚动完成，包含测试初始化、日志生成、加载与搜索，不是纯日志数据结构内存，也不是泄漏或长期运行测试。新版取样后继续验证取消/重搜，后续阶段不计入上述指标。

## 结果

以下均为五次正式样本的中位数，括号内为最小–最大；热身不计入。

| 指标 | 第一组旧版 | 第一组新版 | 第二组旧版 | 第二组新版 |
| --- | ---: | ---: | ---: | ---: |
| 打开 ms | 246（210–249） | 241（217–280） | 251（220–252） | 251（220–256） |
| 搜索 ms | 185（112–187） | 114（114–185） | 186（185–187） | 187（186–188） |
| 100 次滚动 ms | 250（246–261） | 269（251–278） | 246（243–249） | 257（252–259） |
| 峰值工作集 bytes | 337006592 | 342913024 | 339066880 | 331784192 |

原始数值（含热身）：[第一组](round1.csv)、[第二组](round2.csv)。执行时间、路径和二进制哈希：[第一组 manifest](round1-manifest.json)、[第二组 manifest](round2-manifest.json)。

完整 QTest 原始日志保留在验收工作区 `out/ui-vs/perf-comparison-20260914` 和 `out/ui-vs/perf-comparison-20260914-repeat`，不提交构建产物。24 次独立调用均退出 0，匹配数、窗口曝光和绘制断言通过。

## 复现

在旧版独立 worktree 中使用 `git apply --unidiff-zero` 应用适配补丁并构建 `zzlogg_application_translation_test`。补丁使用零上下文格式，精确适用于上述基线。新旧均使用上述编译配置。新版工作区执行（替换两条 exe 路径；输出目录必须不存在）：

```powershell
./tools/compare-ui-performance.ps1 `
  -BaselineExe D:/File/Program/GitCode/ZzLogg/.worktrees/ui-perf-baseline/out/ui-vs/output/Release/zzlogg_application_translation_test.exe `
  -CurrentExe ./out/ui-vs/output/Release/zzlogg_application_translation_test.exe `
  -QtBin D:/SoftWare/Qt/6.11.0/msvc2022_64/bin `
  -OutputDirectory ./out/ui-vs/perf-new-run -Runs 5
```

## 后续边界

- 建议单独剖析轻微的滚动重绘回退；本轮只有观察证据，尚未确定原因。
- 本轮只覆盖单文件和一种命中密度；GB 级、长行、高命中、长期追加仍不能由此推断。
- 多屏、DPI 迁移、拖动吸附仍待人工验收；Linux/macOS 继续暂停。
