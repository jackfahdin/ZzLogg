# 文件监控候选对照

应用继续使用 efsw。本目录的 `QtWatchCandidate` 只编入显式实验目标，不进入应用或 `ci_build`。它同时监控文件和父目录，合并通知，并在注册失败时周期重试。它没有完成生产接口集成、运行中配置切换、资源上限与三平台验证，不能直接用作替代组件。

启用 `KLOGG_BUILD_UI_TESTS`，从仓库根目录构建：

```bash
QT_ROOT=/path/to/Qt/6.11.1/gcc_64 cmake --preset linux-gcc-release -DKLOGG_BUILD_TESTS=ON
cmake --build out/build/linux-gcc-release --target zzlogg_filewatch_probe --parallel 8
QT_QPA_PLATFORM=offscreen ZZLOGG_WATCH_BACKEND=efsw \
  out/build/linux-gcc-release/output/zzlogg_filewatch_probe
QT_QPA_PLATFORM=offscreen ZZLOGG_WATCH_BACKEND=qt \
  out/build/linux-gcc-release/output/zzlogg_filewatch_probe
python3 scripts/dev/benchmark_filewatch.py \
  --binary out/build/linux-gcc-release/output/zzlogg_filewatch_probe
```

Windows 使用 `windows-msvc-release`，在 PowerShell 设置 `$env:QT_QPA_PLATFORM="offscreen"`、`$env:ZZLOGG_WATCH_BACKEND="qt"`（或 `efsw`）。Qt `bin` 需要在 PATH，必要时将 QT_PLUGIN_PATH 指向该 kit 的 plugins。macOS 使用 `macos-clang-release`。runner 的 `--binary` 接受对应可执行文件路径。

`zzlogg_filewatch_test` 是默认 CI 的生产回归目标，CTest 名为 `zzlogg_filewatch.lifecycle`。它测试现有 efsw 的 native／poll／hybrid 模式，既检查通知后重读内容，也检查实际 LogData 的行数和行内容，涵盖追加、截断、同尺寸换行布局变化、轮转、删除重建、原子替换以及后续追加。Qt 候选只进行通知层对照，**尚未与 LogData 或 UI 集成**。

探针复用通知层场景，并增加以下诊断：

- `recoveryReport`：父目录删除重建、登记时父目录不存在。`WATCH_RECOVERY ... false` 表示观察窗口内未恢复，即使 QtTest 最后打印 PASS，也不能把它计为该能力验证通过；这个方法只完成测量，不作准入断言。
- `metrics`：128 个小文件位于同一目录；静置 600 ms 后观察 2 秒空闲 CPU，再每 20 ms 追加一次、持续 2 秒，记录周期通知数和最终内容通知延迟。要求连续写入不会完全饿死刷新，但不以细粒度时延阈值判定后端性能。
- `pollingMetadataBlindSpot`：仅轮询时，同大小且恢复 mtime 的覆盖按当前实现不会被发现；这是已知局限的特征测试，不是内容监控能力的成功用例。对应 native／hybrid 场景另测能够发现覆盖。

本机 CPU 指标为进程所有线程累计时间，包括 QtTest 的事件循环和测试开销。POSIX 使用 std::clock，Windows 使用 GetProcessTimes（MSVC std::clock 是墙钟，不能用它测 CPU）。计时不可用时输出 -1，runner 拒绝汇总。每种模式轮询周期为 100 ms，通知合并窗口为 250 ms；这不同于应用默认 2000 ms 的轮询周期。

runner 默认执行一次 ABBA，即每版每模式只有 2 份样本，保存原始日志、平台、二进制 SHA-256 和中位数。可以增加 `--rounds`。最终写入时刻相对合并窗口的相位会显著影响延迟；测试轮询粒度也会影响读数。没有固定 CPU、锁频或分离 QtTest 开销，不测索引吞吐、渲染延迟或内核资源极限，不能凭这些小样本宣称候选全面更快。

如需测 SMB／NFS／UNC，先挂载或准备可写测试目录，设置 `ZZLOGG_WATCH_TEST_ROOT`；所有测试在其中创建独立临时子目录，退出后清理。必须自行确认该目录确实位于目标远程文件系统。没有设置该变量时只测系统临时目录，不代表网络盘已验证。

本轮结论和未完成条件见 [P9 评估记录](../../../docs/superpowers/plans/2026-11-14-filewatch-evaluation.md)。
