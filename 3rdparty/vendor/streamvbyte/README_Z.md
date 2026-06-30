# streamvbyte 本地化处理说明

## 基本信息

- **库名称**：streamvbyte
- **版本**：1.0.0
- **上游仓库**：https://github.com/lemire/streamvbyte
- **对应标签**：`v1.0.0`
- **作用**：基于 SIMD 指令的高速整数压缩库，klogg 中用于压缩存储行号等整数数据。

## 本次本地化的修改

### 1. 保留核心源码，删除非必要内容

没有引入 streamvbyte 完整仓库，仅保留构建库所需的核心文件：

- `include/`：公共头文件
  - `streamvbyte.h`
  - `streamvbyte_zigzag.h`
  - `streamvbytedelta.h`
- `src/`：实现源码
  - 编码/解码核心实现
  - x64 和 ARM SIMD 加速版本
  - 0124 变体编码
  - delta 编码支持

已删除的内容：

- `examples/`
- `tests/`
- 其他与核心库构建无关的文件

### 2. 使用原始 `CMakeLists.txt`

保留上游的 `CMakeLists.txt`，它直接创建 `streamvbyte` 静态库目标并暴露 `include/` 目录。由于 `examples/` 和 `tests/` 目录已删除，原文件中的相关构建逻辑不会触发。

## CMake 集成说明

`3rdparty/CMakeLists.txt` 通过 `add_subdirectory(vendor/streamvbyte)` 引入本目录。

本目录的 `CMakeLists.txt` 会创建 `streamvbyte` 目标，项目中的 `src/logdata` 等模块可通过 `target_link_libraries(... streamvbyte)` 链接使用。

## 更新步骤

1. 下载或切换到需要的上游标签（例如 `v1.0.0`）。
2. 保留 `include/` 和 `src/` 目录下的核心文件。
3. 删除 `examples/`、`tests/` 等非必要目录。
4. 更新本文件中的版本号。
5. 更新 `3rdparty/vendor/README.md` 总览表格中的版本号。
6. 重新配置并构建 klogg，确认 `logdata` 压缩相关功能正常。
