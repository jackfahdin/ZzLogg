# P13 归档基线与完整性检查

> 使用 writing-plans／executing-plans 内联执行，一次最终独立审查。

**目标／规格：** 延续用户确认的 P13 下一步，建立独立 ZIP／7z／tar 正常归档及便携 ZIP 基线，验证拼接流、padding 和尾随数据。先修复实际归档复制的短读／写失败误报成功，不更换压缩库或自写格式解析器。本地 master 中文提交，不推送。

**设计：** Python 标准库和本机 7-Zip 离线生成小型夹具；CI 仅消费夹具，不增加 Python／7-Zip 运行依赖。新增真实 Decompressor 归档测试和显式单文件流诊断入口，独立预期校验文件树、大小和摘要。已有便携包测试补独立生产者 ZIP。保持归档 API、取消和权限处理，局部修正 vendor 的 KArchiveFile::copyTo 读取／写入／刷新检查；独立夹具另暴露 7z 可选时间字段、空文件目录映射及 tar 短头部缺陷，一并做最小修复并记录本地补丁。替换整个归档库与重做流解码状态机都扩大维护面，本轮不采用；拼接流审计若揭示底层兼容问题，保留真实失败报告作为后续迁移门槛，不把单成员成功当成完整输出。

## 审查重点

- ZIP stored／deflate、7z solid／non-solid、tar 及压缩 tar 的文件树、空文件和 Unicode／二进制内容一致。
- 声明长度超过真实输出时，短读不能写入未初始化内存并返回成功；读取、写入、flush 失败都返回 false。
- 取消、异常与错误路径的输入设备释放；正常归档不受修复影响。
- 独立 ZIP 的便携包安装清单与原目录内容一致；现有保护路径仍通过。
- 拼接流即使返回 true 也要核对全部成员内容；诊断失败明确列出，不写成验收通过。路径安全和超大归档政策仍单独待验收。

### Task 1: 独立格式回归、复制完整性修复与流诊断

- [x] 新建 `tests/ui/fixtures/archives/` 独立夹具与生成说明、`tests/ui/archive_test.cpp`、CMake 注册及便携包独立 ZIP 用例。先运行旧实现，Expected: 正常格式通过，错误长度复制暴露失败。
- [x] 修复 `3rdparty/vendor/klogg_karchive/karchive/src/karchive.cpp` 的 copyTo 读写、flush 与设备所有权；更新 README_Z 补丁记录。Expected: 新归档用例通过。
- [x] 显式诊断逐项核对 gzip／bzip2／xz 拼接、XZ padding、尾随数据及 LZMA，保存完整状态和输出摘要；有差异时报告非零退出并记录准确边界。
- [x] 默认 ci_build、完整 CTest／Python CI；一次独立审查，记录结论并中文提交。最终任务验证：`ctest --test-dir out/build/p6-boolean -R 'zzlogg_ui.archive|zzlogg_ui.decompressor|zzlogg_portable.package' --output-on-failure`。


## 复现与局部修复

旧实现通过六种 ZIP／tar 正常夹具后，在无修改时间字段的合法 7z 上崩溃。补上可选字段缺省判断后，solid／non-solid 两种 7z 均暴露空文件被解压到错误目录：空文件与下一文件共享数据偏移，原偏移到目录的映射互相覆盖。改为以文件对象为键，仍按原顺序读取归档。

另有三个失败用例先证明旧行为不正确：ZIP 声明的成员长度大于实际输出仍报成功、真实 `/dev/full` 缓冲写入失败仍报成功、仅剩 8 字节的 tar 头部仍报成功。复制现在按实际读取量递减，拒绝读取错误／提前结束、短写和刷新失败，并用 RAII 释放输入设备；tar 拒绝非零且不足 512 字节的头部。保留取消与权限语义，不重写格式解析器。

## 独立基线与验证

Python 标准库离线生成 stored／deflate ZIP、tar／tar.gz／tar.bz2／tar.xz；7-Zip 离线生成 LZMA2 solid／non-solid 7z，省略时间属性。八种归档均含子目录、中文路径／内容、NUL、CRLF 和空文件，预期逐字节内容及摘要写入 manifest。另一个独立 ZIP 通过实际便携更新包 preparePackage 安装路径验证，确认提取的程序内容和原安装内容。CI 只读取已提交夹具，不增加运行时或构建依赖。

新增归档 Qt Test **18 项通过**（含初始化／清理），覆盖八种正常格式、错误长度、三种格式的截断／预取消以及真实写满错误。默认 ci_build、离屏 **101/101 CTest** 通过；Python CI **149 项、3 项跳过**，无失败。八个归档另由 Python／7-Zip 独立解包并核对 manifest 和全部成员内容。`/dev/full` 不存在的平台仅跳过此设备用例。未做吞吐量对比，也未进行额外 Windows 验证。

## 显式流诊断：仍有未修复问题

运行命令：

```sh
python3 scripts/dev/audit_compressed_streams.py \
  --binary out/build/p6-boolean/output/zzlogg_archive_test \
  --output out/benchmarks/p13-archive-baseline/streams-final
```

最终 **16 项中 6 项通过、10 项存在差异，退出码为 1**；这不是验收通过。每项均通过真实 Decompressor，并独立核对完整输出、摘要、完成次数和返回结果。报告与输入／输出留在上述目录。

| 场景 | 结果 |
| --- | --- |
| gzip 两成员、大首成员、空首成员及尾随垃圾拒绝 | 4 项通过 |
| XZ 单流后四字节零 padding、旧 LZMA | 2 项通过 |
| bzip2／xz 两成员、大首成员、空首成员 | 6 项只输出第一成员却返回成功 |
| XZ 成员间合法 padding | 1 项只输出第一成员却返回成功 |
| bzip2／xz 尾随垃圾、XZ 非法单字节 padding | 3 项未满足严格拒绝目标 |

其中 **7 项是合法多成员输入缺少后续输出**，下一轮优先修复；其余 3 项属于严格尾部处理要求，不据此声称所有宽松命令行工具违反格式规范。当前调用层检查无法发现解码设备主动结束且报告 EOF 的缺失，需单独处理底层流状态机。保留当前依赖，不用本轮正常单成员测试为替换／升级背书；仅阅读官方 v6.31.0 源码也不能证明升级可自动解决问题。

最终诊断身份：

- binary SHA-256：`b710eb8cd81b21ee3404e38ce6c78c193b837f108fe289851396c71690fc9388`
- script SHA-256：`f253eccce0e68f9330b7a0ae72bfdb5da7aa4cf143bd12436cb44c24411bc172`
- results.json SHA-256：`3230ffe417165f636af1da2360d52874b95e23b5992780169895a795f9deb56c`

路径安全、超大归档政策、完整损坏格式矩阵与 codec 性能比较仍待独立验收。P13 整体保持进行中；本轮验证日志归档到 `out/benchmarks/p13-archive-baseline/validation/`。


## 独立审查与处理

一次全新上下文只读审查未发现生产补丁的新缺陷，指出便携 ZIP 用例只检查文件数及 EXE 内容，尚不足以证明完整清单一致。已补齐全部三个成员的路径／大小／SHA-256、实际 payload 全文件内容，以及原安装目录（含配置和额外用户文件）的完整快照比较。临时破坏非 EXE 的 portable marker，新增断言确实失败；撤销注入后 ci_build、完整 101/101 CTest 以及 Python 149 项（3 项跳过）再次通过。此项是测试验收缺口，不宣称发现新的安装器生产缺陷。

审查未判断的底层拼接流修复、通用归档路径／符号链接／路径竞态安全、超大归档／资源策略及 Windows／macOS 运行行为，仍分别留待后续。代价是本轮不为这些场景提供新的兼容、安全或跨平台结论；已知 bzip2／xz 拼接流缺失继续作为下一优先项。无延后的 Minor。
