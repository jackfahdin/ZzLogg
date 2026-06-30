# hyperscan 本地化处理说明

## 基本信息

- **库名称**：hyperscan
- **来源提交**：`0931a40e0cf1d7f92189bc546c3491ed5c113f8b`（短号 `0931a40`）
- **上游仓库**：https://github.com/variar/hyperscan（klogg 维护的 fork）
- **作用**：高性能正则表达式匹配引擎，klogg 中用于加速大规模日志的正则搜索。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 hyperscan fork 的完整源码作为子目录放入 `3rdparty/vendor/hyperscan/`，避免 CMake 配置时从 GitHub 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载：

```cmake
cpmaddpackage(
  NAME
  hyperscan
  GITHUB_REPOSITORY
  variar/hyperscan
  GIT_TAG
  0931a40e0cf1d7f92189bc546c3491ed5c113f8b
  EXCLUDE_FROM_ALL
  YES
)
```

改为本地 `add_subdirectory`：

```cmake
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/hyperscan)
```

由于上游 `hyperscan/CMakeLists.txt` 已经注释掉了 `unit`、`tools`、`chimera`、`doc/dev-reference` 的 `add_subdirectory`，此处只需关闭 `BUILD_EXAMPLES` 即可避免构建示例。

### 3. 调整 wrapper 的 include 路径

`klogg_hyperscan` wrapper 原本使用 `${hyperscan_SOURCE_DIR}/src`，改为使用本地路径：

```cmake
add_library(klogg_hyperscan INTERFACE)
target_link_libraries(klogg_hyperscan INTERFACE hs)
target_include_directories(klogg_hyperscan INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/hyperscan/src)
```

## 更新步骤

1. 从上游 fork 获取目标提交（例如 `0931a40`）。
2. 将源码放入 `3rdparty/vendor/hyperscan/`。
3. 更新本文件中的来源提交 hash。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的 commit hash。
5. 重新配置并构建 klogg，验证正则搜索功能正常。
