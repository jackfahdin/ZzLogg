# vectorscan 本地化处理说明

## 基本信息

- **库名称**：vectorscan
- **来源提交**：`d29730e1cb9daaa66bda63426cdce83505d2c809`（短号 `d29730e`）
- **上游仓库**：https://github.com/VectorCamp/vectorscan
- **作用**：Hyperscan 的开源分支/移植版，支持 x86、ARM、RISC-V 等更多架构，klogg 中用于在 Hyperscan 不可用的平台上提供高性能正则搜索。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 vectorscan 完整源码作为子目录放入 `3rdparty/vendor/vectorscan/`，避免 CMake 配置时从 GitHub 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载并传递大量构建选项：

```cmake
cpmaddpackage(
  NAME
  vectorscan
  GITHUB_REPOSITORY
  VectorCamp/vectorscan
  GIT_TAG
  d29730e1cb9daaa66bda63426cdce83505d2c809
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_STATIC_LIBS ON"
  "BUILD_UNIT OFF"
  "BUILD_TOOLS OFF"
  "BUILD_EXAMPLES OFF"
  "BUILD_BENCHMARKS OFF"
  "BUILD_DOC OFF"
  "BUILD_CHIMERA OFF"
  "BUIlD_AVX2 OFF"
  "BUIlD_AVX512 OFF"
  "BUIlD_AVX512VBMI OFF"
  "FAT_RUNTIME OFF"
)
```

改为本地 `add_subdirectory`，并在调用前强制设置相关缓存选项：

```cmake
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(FAT_RUNTIME OFF CACHE BOOL "" FORCE)
set(BUILD_AVX2 OFF CACHE BOOL "" FORCE)
set(BUILD_AVX512 OFF CACHE BOOL "" FORCE)
set(BUILD_AVX512VBMI OFF CACHE BOOL "" FORCE)
set(BUILD_CHIMERA OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/vectorscan)
```

### 3. 调整 wrapper 的 include 路径

`klogg_vectorscan` wrapper 原本使用 `${vectorscan_SOURCE_DIR}/src` 和 `${vectorscan_BINARY_DIR}`，改为使用本地路径：

```cmake
add_library(klogg_vectorscan INTERFACE)
target_link_libraries(klogg_vectorscan INTERFACE hs)
target_include_directories(klogg_vectorscan INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/vectorscan/src
  ${CMAKE_CURRENT_BINARY_DIR}/vendor/vectorscan
)
```

### 4. 修改 `vectorscan/CMakeLists.txt`

为避免作为 klogg 子目录构建时拉入测试和文档，对 `vectorscan/CMakeLists.txt` 做了以下调整：

- 注释 `add_subdirectory(unit)`
- 注释 `add_subdirectory(doc/dev-reference)`
- 将 `BUILD_EXAMPLES` 和 `BUILD_BENCHMARKS` 两个 `option` 的默认值改为 `OFF`

> 注：`tools` 和 `chimera` 使用 `${CMAKE_SOURCE_DIR}` 判断，作为子目录引入时指向 klogg 根目录，自然不会被加入。

## 更新步骤

1. 从上游仓库获取目标提交（例如 `d29730e`）。
2. 将源码放入 `3rdparty/vendor/vectorscan/`。
3. 重新应用上述对 `vectorscan/CMakeLists.txt` 的修改。
4. 更新本文件中的来源提交 hash。
5. 更新 `3rdparty/vendor/README.md` 总览表格中的 commit hash。
6. 重新配置并构建 klogg，验证正则搜索功能正常。
