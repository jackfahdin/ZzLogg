# Windows 本地更新测试入口

目标：在用户已配置 MSVC 的 Windows 电脑上，以 `D:\SoftWare\Qt\6.12.0` 为默认 Qt 位置，一条命令增量构建并打包，另一条命令提供本地更新源并启动已安装的测试客户端。

复用现有 CMake `BUILD_VERSION` 参数生成两个不同显示版本，不修改源码版本、不创建 GitHub Release。共用一个专用构建目录并关闭 LTO；安装包按版本保存。打包在 `out/local-windows` 内复制现有 Inno 脚本与图标，保持原打包逻辑，避免清理仓库根目录的 `release/txpayload`。本地启动传入 Test 更新源及独立数据目录，正式源关闭。安装包仍使用产品的安装登记，不承诺与正式安装并存。

- [x] 扩展 `scripts/dev/local_update_feed.py`：独立输出目录、启动客户端时注入测试环境；先绑定本地端口，再启动应用。
- [x] 新增 `scripts/dev/windows_local_update.py`：Qt kit 识别、工具预检、增量构建、隔离打包、版本产物校验和 serve 入口。
- [x] 增加 Python 行为测试：真实签名与 HTTP 下载、子进程环境、Qt 目录选择、隔离打包、篡改拒绝及端口占用时不启动客户端。
- [x] 新增 Windows 操作说明并链接到 BUILD.md；运行 Python 测试及脚本语法检查，明确本机不能执行 MSVC/Inno 原生构建。

验证：105 项 Python 测试通过，py_compile 与 git diff --check 通过。HTTP 服务/下载/验签和子进程环境使用真实进程验证；Windows MSVC/Inno 未在本机执行。独立审查确认相对打包目录及版本身份一致，补充了管理员终端可能要求手动重启的运行提示。
