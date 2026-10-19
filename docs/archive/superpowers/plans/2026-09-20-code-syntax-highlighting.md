# 代码文本高亮实现计划

**目标：** 在现有大文件自绘视图中增加自动识别及手动选择的 C++、Java、JSON 高亮，仅本地试用，不提交推送。

**架构：** 每个文档共用 KSyntaxHighlighting 词法状态，主视图和过滤结果使用原文件行号查询。定时器分批解析，未完成状态显示纯文本；检查点与有界缓存支持回滚，搜索、自定义规则和选区覆盖语法颜色。

**技术栈：** Qt 6.8+、静态 KSyntaxHighlighting 6.22.0（内嵌语法资源）、Qt Test。

## 1. 依赖和词法服务
- [x] 在 `cmake/ZzSyntaxHighlighting.cmake` 固定下载地址与 SHA256，隔离上游 CMake 选项，并打包许可证。
- [x] 在 `tests/ui_acceptance/syntaxhighlighttest.cpp` 先写自动识别、多行状态、随机请求、刷新和超长行降级测试，运行确认尚未实现。
- [x] 新增 `src/ui/include/codesyntax.h` 和 `src/ui/src/codesyntax.cpp`：提供 setFileName、setLanguage、invalidate、formats 接口；解析以原始行号和 UTF-16 列号为准。
- [x] 每批最多 32 行或 3 ms；最多解析前 100000 行，单行上限 16384 字符；遇到超长行后保守保持纯文本，避免伪造后续词法状态。缓存至多 4096 行，每 1024 行保存检查点。

## 2. 视图和操作入口
- [x] 修改 `abstractlogview.*` 将语法作为最低优先级颜色层；解析更新时废弃滚动图像缓存。
- [x] 修改 `crawlerwidget.*` 共享文档服务，文件更新、编码和预处理变化时刷新；保存每个文档语言选择。
- [x] 修改 `documentworkspace.cpp` 在打开和恢复时提供文件名。
- [x] 修改 `mainwindowmenus.cpp` 增加“语法高亮”菜单及自动、纯文本、C++、Java、JSON，补充三种 UI 语言翻译。

## 3. 验证和交付
- [x] 测试真实文件主视图及过滤视图、选择覆盖、滚动缓存，执行完整 CTest。
- [x] 编译应用并检查依赖静态嵌入与部署许可证，检查 Qt 6.8 配置兼容性。
- [x] 独立审查改动并处理问题，记录本地试用入口及降级边界。

## 本地验证结果

- Qt 6.11.2：`cmake --build /tmp/zzlogg-qt6-build --target ci_build -j 4` 成功。
- `QT_QPA_PLATFORM=offscreen ctest --test-dir /tmp/zzlogg-qt6-build --output-on-failure -j 4`：82/82 通过。
- Qt 6.8.3：项目配置和 `KF6SyntaxHighlighting` 目标编译通过（未重建整套应用）。
- `cmake --install /tmp/zzlogg-qt6-build --prefix /tmp/zzlogg-syntax-install` 成功，确认高亮许可证随安装产物提供；高亮库静态链接。
- 独立审查发现的密集片段绘制开销、缓存抖动、超长行预读、CRLF 续行和最低 CMake 版本问题均已处理。
- Windows/macOS 的构建与打包仅检查接线，尚未执行远端 CI。
- 初次实现仅在本地验证；后续发布版本为 `v26.09.01`。
