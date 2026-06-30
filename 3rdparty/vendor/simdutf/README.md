# simdutf（本地 amalgamation 版本）

本目录存放项目依赖的 **simdutf** 库的本地副本，用于在无法访问 GitHub 的开发环境中离线构建。

## 版本信息

- **库名称**：simdutf
- **版本**：5.6.2
- **对应标签**：`v5.6.2`
- **上游仓库**：https://github.com/simdutf/simdutf
- **分发形式**：amalgamation（合并后的单头文件分发版）

## 目录文件

- `simdutf.h` —— 公共 API 声明（由上游 `include/` 目录合并生成）
- `simdutf.cpp` —— 实现代码（由上游 `src/` 目录合并生成）
- `amalgamation_demo.cpp` —— 上游自带的示例文件
- `CMakeLists.txt` —— 本地 CMake 构建定义，向上层暴露 `simdutf` 目标

## 为什么要本地存放？

上游原本通过 CPM 在 CMake 配置阶段从 GitHub 自动下载 simdutf。在部分开发主机上外网受限，下载会失败。将 amalgamation 文件直接纳入 git 管理后，无需联网即可配置和构建。

## 文件来源

当前文件取自上游仓库 `v5.6.2` 标签下的 `singleheader/` 目录：

```text
https://github.com/simdutf/simdutf/tree/v5.6.2/singleheader
```

如果你需要从源码重新生成 amalgamation 文件，可以在完整克隆的上游仓库中使用 **Python 3** 执行：

```bash
python3 ./singleheader/amalgamate.py
```

## 更新步骤

1. 下载或切换到需要的上游标签（例如 `v5.6.2`）。
2. 将 `singleheader/simdutf.h` 和 `singleheader/simdutf.cpp` 复制到本目录，覆盖旧文件。
3. 修改本 `README.md` 中的版本号。
4. 重新配置并构建，运行与 logdata 编码转换相关的测试确认无误。

## CMake 集成说明

`3rdparty/CMakeLists.txt` 通过 `add_subdirectory(vendor/simdutf)` 引入本目录。

本目录的 `CMakeLists.txt` 会创建一个名为 `simdutf` 的静态库目标，供 `src/logdata` 等模块通过 `target_link_libraries(... simdutf)` 链接使用。
