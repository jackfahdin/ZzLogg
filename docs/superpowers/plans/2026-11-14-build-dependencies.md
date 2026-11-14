# P7：构建依赖传播及搜索配置

**目标：** 缩小不必要的编译依赖，明确语言标准及后端选择，保留既有搜索能力。

**范围与方案：** 从 `802ab95` 出发，在现有主线继续。选择按公开头文件逐项收紧依赖、显式配置标准／加速构建；不把所有链接一律改为 PRIVATE，也不统一强制所有第三方使用 C++20。工具库的 QApplication／QWidget 接口仍需公开 Widgets，日志头文件仍需公开 Core5Compat、TBB、CRoaring，搜索头文件中的 hs 类型仍需公开加速库。Network 和 Concurrent 移至真实使用方。

**语言标准：** 修正 project_options 的过时 C++14 声明为 C++17。顶层继续以 C++17 构建核心；ZzPureTools／KSyntaxHighlighting 使用 C++20，UI 依赖传播会提升消费者标准。整个应用仍需支持 C++20 的工具链。

**搜索配置：** 裸 CMake 和基础预设默认 Qt，两个加速开关互斥、同时开启报错。提供 Linux Vectorscan、Windows Hyperscan、macOS Vectorscan 的命名 Release 预设。CI 显式保留原有 Windows Hyperscan／macOS Vectorscan，Linux 和 CodeQL 明确 Qt；更新 smoke 保留 Hyperscan。所有配置输出选定后端。增加真实 matcher 类型和结果测试，防止加速构建实际静默回退却被误判为覆盖。

## 执行与验证

- [x] 编译只链接 `klogg_utils` 的公开头文件消费者；验证旧图会泄漏 Network／Concurrent，然后收紧传播并使其通过。
- [x] 按公开头文件检查 utils、regex、logdata、filewatch、support 与 UI 的链接作用域，补充使用方直接依赖；全量构建发现隐式依赖缺口。
- [x] 更新 `cmake/CompilerFlags.cmake`，增加 `cmake/ZzSearchBackend.cmake`；配置测试覆盖默认 Qt、单独 Hyperscan／Vectorscan、冲突错误及已有缓存选择。
- [x] 增加独立搜索后端测试：普通构建断言 Qt matcher；加速构建断言单／多模式的 hs matcher，比较匹配顺序、大小写、Unicode、空行和 lookahead 回退结果。
- [x] 更新 `CMakePresets.json` 及 CI 显式开关，保留加速测试入口；更新构建指南和总路线。
- [x] Linux Qt 全量 `ci_build`／CTest、Python CI；本机 Vectorscan 显式配置、构建和执行真实搜索测试。安装缺失的 Boost／Ragel 仅下载并解包到忽略目录，不改系统依赖。
- [x] 独立代码审查、中文提交，不推送。Windows／macOS 原生检查留给 CI。

## 复现命令

```bash
QT_ROOT=/home/zz/Qt/6.11.1/gcc_64 cmake --preset linux-gcc-release \
  -B out/build/p6-boolean -DKLOGG_BUILD_TESTS=ON -DWARNINGS_AS_ERRORS=ON
cmake --build out/build/p6-boolean --target ci_build --parallel 12
QT_QPA_PLATFORM=offscreen ctest --test-dir out/build/p6-boolean --output-on-failure --parallel 8
python3 -m unittest discover -s tests/ci
QT_ROOT=/home/zz/Qt/6.11.1/gcc_64 cmake --preset linux-gcc-vectorscan-release \
  -DBOOST_ROOT="$PWD/out/benchmarks/p7-build-tools/root/usr" \
  -DRAGEL="$PWD/out/benchmarks/p7-build-tools/root/usr/bin/ragel"
cmake --build --preset linux-gcc-vectorscan-release --target zzlogg_search_backend_test --parallel 8
ctest --preset linux-gcc-vectorscan-release -R '^zzlogg_build.search_backend$'
```

## 结果

已完成（2026-11-14）。本轮没有移除运行功能或第三方库；主要收益是缩小编译使用需求的传播，减少隐式依赖和后端选择歧义，不承诺包体或运行速度改善。

| 模块 | 调整 | 保留的公开依赖依据 |
| --- | --- | --- |
| utils | 移除 Network／Concurrent | clipboard、openfilehelper、active_screen、dispatch_to 等公开头仍使用 Gui／Widgets；containers 使用 type_safe／mimalloc |
| regex | logging／settings 改为 PRIVATE | patterns、matcher、容器及 hs 类型仍需 utils、Qt Core 和选定加速库 |
| logdata | support／settings／uchardet／simdutf／streamvbyte 改为 PRIVATE；去掉重复 filedigest.cpp 条目 | 公开日志头使用 Core5Compat、TBB、CRoaring、type_safe 和 filewatch |
| filewatch | settings／efsw 改为 PRIVATE | 公开类型使用 QObject，底层 efsw 已封装在实现中 |
| support | 仅 Qt Core 保持 PUBLIC，其他依赖 PRIVATE | IssueReporter 公开 QString／QUrl，GUI／版本／内存探测仅用于实现 |
| UI | 显式 PUBLIC settings／Network；PRIVATE Concurrent／support／simdutf | downloader.h 暴露网络类型；高亮实现直接使用 simdutf |

工具接口消费者测试首次在旧配置下因 `QT_NETWORK_LIB`／`QT_CONCURRENT_LIB` 被定义而失败，收紧后通过。完整构建发现 UI 的 simdutf 使用原本依赖 logdata 间接传播，已补成直接私有链接。集成测试入口把宽泛的 QtConcurrent include 改为实际使用的 QThreadPool。

后端配置测试在隔离的真实 CMake cache 中验证默认 Qt、各单独加速选项、重复配置保留用户选择，以及双开关冲突诊断。独立后端测试在标准构建断言 Qt matcher，在 Vectorscan 构建断言 HsSingleMatcher／HsMultiMatcher，并核对匹配结果及 lookahead 精确回退；CI 中也保留该检查。

验证环境：Linux／GCC 15／Qt 6.11.1，Warnings as Errors 开启（既有测试警告策略不变）。

- 标准配置 `ci_build` 成功；96 项 CTest 全通过（13.23 秒）。
- 全新 Vectorscan 输出目录配置，编译真实 vendor 加速库和两个独立消费者目标成功；3 项 `zzlogg_build.*` 全通过。没有在本机执行该配置下的完整 GUI 测试套件。
- Python CI 124 项完成、3 项跳过，无失败。
- `compile_commands.json` 确认核心求值器和 utils 消费者使用 C++17、UI 使用 C++20；utils 消费者不再收到 Network／Concurrent 定义。
- 独立审查无剩余阻塞问题；新增测试放在 `tests/cmake/`，避免仓库的 `build/` 忽略规则漏交文件。Windows 测试显式补 Qt DLL 搜索路径。
- Windows Hyperscan、macOS Vectorscan 的 CI 配置能力保留，但原生编译、运行与打包尚未在本机验证，等待推送后的 CI。

日志保存在本地 `/tmp/zzlogg-p7-*.log`；Boost 1.83 和 Ragel 6.10 解包到 `out/benchmarks/p7-build-tools/root`，不进入 Git、不修改系统安装。使用上述独立目标可复现检查。更新构建指南与三语手册，中文提交、不推送。
