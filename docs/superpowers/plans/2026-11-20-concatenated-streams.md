# P13 拼接压缩流完整性修复

> 使用 writing-plans／executing-plans 内联执行，最后一次全新上下文独立审查。

**目标／规格：** 延续用户“继续”授权与上一轮明确的下一项，gzip／bzip2／xz 合法多成员必须完整输出，空成员和分块边界不影响结果；损坏／截断后续成员及垃圾尾部返回失败。XZ 合法零 padding 交由 liblzma 校验；旧 LZMA 单流仍可读，不支持其拼接。保留异步取消和一次完成语义。本地 master 中文提交、不推送。

**实现方案：** 修复现有 KCompressionDevice 与 filter 的缓冲区交接。bzip2／gzip 在成员结束后保留未消费输入，只有解码结束且缓冲及底层设备都耗尽才算 EOF；gzip 统一由设备管理跨成员，不在 filter 内重复实现。XZ AUTO 模式使用现有 liblzma 的 LZMA_CONCATENATED 和明确 EOF 的 LZMA_FINISH；原 7z raw LZMA／LZMA2 路径仍按原调用。整库替换缺少可对比基线，自写 padding／格式解析增加维护面，本轮均不采用。不改变 codec 版本及内存上限，不宣称性能提升。

**文件：** vendor kcompressiondevice.cpp、kgzipfilter.cpp、kxzfilter.cpp/h 负责读取与结束状态；tests/ui/decompressor_test.cpp 及独立 streams 夹具负责应用契约，新增直接设备测试覆盖小输出缓冲、分块、seek 与错误输入；README_Z 与路线图记录补丁和范围。独立生成器只离线使用 Python，CI 消费提交夹具。

## 审查重点

- 成员边界在 8 KiB 输入块内／块末／块后，输出恰满和空成员不漏后续字节。
- XZ padding 跨输入块、非法长度、第二成员损坏／截断均准确处理；旧 LZMA 与 7z raw 解码不回归。
- 读取错误与 EOF 不混淆，零进展有界退出；已有取消和完成信号语义不变。
- seek(0)、重新打开和小块读取后结果一致，不残留上一轮成员状态或指针。
- 完整回归及独立 16 项审计全部通过才解除已知缺口，不把成功标志代替逐字节内容。

### Task 1: 拼接流状态与独立回归

- [x] 提交独立小型拼接夹具，覆盖三格式多／空成员、大首成员、坏尾成员、padding／旧 LZMA；新增默认 CTest 用例。先在旧实现运行，Expected: 复现缺失输出及尾部误报成功，现有单流通过。
- [x] 修正设备成员状态、负读取及 EOF 完成；bzip2／gzip 重新初始化保留剩余输入并检查返回值；XZ 使用原生拼接及 finish。Expected: 新用例及原归档／便携回归通过。
- [x] 运行独立 audit_compressed_streams.py、默认 ci_build、完整 CTest、Python CI；独立审查并处理发现。Expected: 16/16 审计和全套通过，记录限制。
- [x] 更新文档、中文提交并归档验证日志。最终任务验证：`ctest --test-dir out/build/p6-boolean -R 'zzlogg_ui.compression_device|zzlogg_ui.decompressor|zzlogg_ui.archive|zzlogg_portable.package' --output-on-failure`。


## 根因与修复

旧设备把底层文件 atEnd 当成所有压缩成员结束，却没有检查已经读入的剩余压缩字节；重新初始化 filter 又清空了输入。gzip 在 filter 内另有拼接循环，bzip2／xz 没有，造成行为不一致。XZ AUTO 未启用原生拼接模式，结束时也没有显式 FINISH，因此成员后 padding 和垃圾未被完整校验。

现在设备按实际输入长度保存缓冲，在成员结束后恢复剩余字节并检查初始化结果；只有成员完成、输入缓冲耗尽且底层 EOF 才返回成功。gzip 在统一边界上处理下一成员。XZ 由 liblzma 的 CONCATENATED／FINISH 负责合法拼接及 padding，不自写格式解析。同步处理负读取和零进展，重开时清除上一轮输入／位置／错误状态。

## 验证记录

46 个新独立输入由 Python 标准库离线生成，CI 直接使用已提交文件；30 个有效输入另由 Python gzip／bz2 和系统 xz 完整解码，与固定 payload 文件逐字节比较，全部输入 SHA-256 与 manifest 一致。系统 xz 用于 padding 复核，因为 Python lzma.decompress 对单流后的零 padding 会另尝试解码并报短流，不能直接充当这些有效样本的 oracle。

修复前应用测试 **44 通过／26 失败**，直接设备测试 **18 通过／16 失败**，明确复现丢失后续内容和坏尾部误报成功。修复后应用测试共 70 项、设备测试共 45 项通过（各含初始化／清理）。设备测试覆盖受控读取在首成员前／末／后分块、1 字节输出、16 KiB 输出恰满、三格式每次 1／5／9 字节输入、seek(0)、重新打开和输入错误后的重开；原归档与便携包同时通过。

默认 ci_build 和完整 **102/102 CTest** 通过；Python **149 项、3 项跳过**，无失败。原独立流审计 **16/16 通过，退出码 0**，未修改审计预期来适配实现。初始失败记录保留在上一轮目录，最终报告在 `out/benchmarks/p13-concatenated-streams/review-final/`，验证日志归档到 `out/benchmarks/p13-concatenated-streams/validation/`。

未升级 codec，不宣称性能收益；通用归档路径／符号链接／超大资源策略及额外 Windows／macOS 运行验证仍不在本轮验收范围。gzip 明确非压缩文本的历史透传行为保持不变，并在短源读取下验证。


## 独立审查

一次全新上下文审查发现 gzip 初次源读取不足 10 字节时，会将完整压缩内容当普通文本返回，原直接设备测试遗漏了 gzip 单字节输入。此问题虽然已有，但与本轮分块边界契约相交，一并修复。

先补三格式每次 1／5／9 字节读取：gzip 三项失败，实际返回原始压缩数据，其他新项通过。设备现在保留不足以识别的初始头部并继续读取，在可以判断前不切换到透传；同时覆盖短、长非压缩文本以保护历史行为。最终设备测试 **45/45**，应用解压测试 **70/70**；最终 ci_build、102/102 CTest、Python 149 项（3 项跳过）以及 16/16 独立审计再次通过。

审查没有其他 Critical／Important／Minor。未判断的 Windows／macOS 运行、codec 内存上限／写入能力／性能及事件驱动 socket 生命周期保持既定范围：本轮验证文件解压，不为未测试的平台、编码路径或异步网络输入提供新保证。代价是这些场景仍可能存在未发现的问题，保留到对应专项验证；没有延后的 Minor。

最终审计身份：

- binary SHA-256：`206623e502ec9cb8af17de003196e502851e5650ddd956390f45c8975911c4ee`
- script SHA-256：`f253eccce0e68f9330b7a0ae72bfdb5da7aa4cf143bd12436cb44c24411bc172`
- results.json SHA-256：`39beef5eff160b22b9b163e7f9eac2ac910a8e7f30f45480b9b2e8717c027c06`
