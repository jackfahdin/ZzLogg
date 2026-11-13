# P4：精简信号节流依赖

**目标：** 将两个实际使用的尾沿节流调用迁移到基于 QTimer 的小组件，移除 KDToolBox 源码和构建目标，保持通知行为。

**基线：** `5fcff8c`。P3 已关闭，maddy 保留。

## 行为约定

- 首次请求启动单次定时器；活动窗口内的请求合并，不能重启定时器。
- 空闲时不发送通知；单次及突发请求在窗口末尾发送一次。
- 持续输入仍定期触发，不等输入停止；触发回调中的新请求开启下一窗口。
- 最新进度（包括 100%）通过调用方保存的状态发送，搜索结束的通知上限仍为一个节流窗口。
- 文件变化继续使用 250 ms，搜索进度继续使用 100 ms；使用 QTimer 默认的 CoarseTimer。
- 保留上游析构语义：若仍有待发请求，在析构期间同步发送一次；没有待发请求则不发送。停止计时后发信号，回调返回后不再访问成员，销毁完成后无定时回调。
- 新组件与计时器同线程；调用方现有的 Qt 信号连接和主线程派发保持不变。
- 不引入 leading/debounce 模式、QML 属性、动态策略等未使用接口。

## 执行步骤

- [x] 在 `tests/ui_acceptance/utils/signalthrottlertest.cpp` 建立行为用例，先用现有 KDSignalThrottler 验证基线。
- [x] 在 `src/utils/include/signalthrottler.h` 和 `src/utils/src/signalthrottler.cpp` 实现最小组件，使用相同测试验证。保留上游 MIT 归属说明。
- [x] 迁移 `src/filewatch` 和 `src/logdata` 的类型及连接，删除对应 `kdtoolbox` 链接。
- [x] 删除 `3rdparty/CMakeLists.txt` 中的目标和 `3rdparty/vendor/KDToolBox/`，更新依赖表与 NOTICE；KDSingleApplication 的许可证继续保留。
- [x] 全新目录构建 `ci_build`，运行全部 CTest 与 CI Python 测试。记录持续事件、重入、最终值和析构用例结果。
- [x] 更新总路线并使用中文提交信息提交。

## 验证方式

先在 `out/build/kde631-vendor` 构建并运行专用 Qt Test；上游和替代实现使用相同用例。计时测试使用信号计数和宽松等待期限，不以精确毫秒数比较平台调度器。持续事件用例要求输入尚未停止时已发送至少三次，能识别错误的防抖实现。

最终使用新的 `out/build/p4-throttler`，Qt 6.11.1、Release、LTO OFF、WARNINGS_AS_ERRORS ON、两组测试开关 ON、Hyperscan/VectorScan OFF。运行 `ci_build`、全量 CTest（offscreen）及 `python3 -m unittest discover -s tests/ci`。Windows/macOS 原生验证留给 CI。

## 结果

- 原 KDSignalThrottler 通过行为基线测试；新组件空实现的 single、burst、pending 析构三项按预期失败，补齐实现后同一组测试通过。
- 新组件仅维护一个 QObject 子定时器；单次计时器的活动状态表示待发请求。Qt 在 timeout 前将其置为空闲，因此重入请求可开启下一窗口，回调删除组件后无需继续访问成员。
- 已移除 KDToolBox 的 286 个受跟踪文件，共 895,680 字节（约 0.85 MiB），新增小型组件及行为测试。保留上游 MIT 归属，KDSingleApplication 声明不变。不以源码删除量推断安装包大小或运行时性能收益。
- 全新目录 `out/build/p4-throttler` 的 `ci_build` 通过（Linux x86_64、GCC 15.2.0、Qt 6.11.1、CMake 4.3.3，配置如上）。最终源文件确认构建后，`QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/p4-throttler --output-on-failure --parallel 8`：93/93 通过，包含原有文件追加读取、搜索完成进度和新增节流用例。
- `python3 -m unittest discover -s tests/ci`：124 项，3 项跳过，整体通过。
- 独立代码审查未发现阻碍合入的问题；`git diff HEAD --check` 通过。Windows/macOS 原生构建尚未运行，待推送后由 CI 验证。
