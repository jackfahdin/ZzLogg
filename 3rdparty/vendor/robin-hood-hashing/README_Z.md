# robin-hood-hashing 本地化处理说明

## 基本信息

- **库名称**：robin-hood-hashing
- **版本**：3.11.5（`src/include/robin_hood.h` 中 `ROBIN_HOOD_VERSION_MAJOR.MINOR.PATCH` 定义）
- **上游仓库**：https://github.com/martinus/robin-hood-hashing
- **作用**：提供 `robin_hood::unordered_map` / `robin_hood::unordered_set`，作为 `std::unordered_map` / `std::unordered_set` 的更快、更省内存的替代方案。klogg 中多个模块用它优化哈希表性能。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 robin-hood-hashing 完整源码放入 `3rdparty/vendor/robin-hood-hashing/`，避免 CMake 配置时从 GitHub 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载，并在未找到时创建 imported target：

```cmake
cpmaddpackage(
  NAME
  robin_hood
  GITHUB_REPOSITORY
  martinus/robin-hood-hashing
  GIT_TAG
  3.11.2
  EXCLUDE_FROM_ALL
  YES
)
if(NOT TARGET robin_hood)
  message("Adding imported target for robin_hood")
  add_library(robin_hood INTERFACE)
  target_include_directories(robin_hood INTERFACE ${ROBIN_HOOD_INCLUDE_DIRS})
endif(NOT TARGET robin_hood)
```

改为直接引入本地版本：

```cmake
# robin_hood
add_subdirectory(vendor/robin-hood-hashing)
```

上游 `CMakeLists.txt` 在被其他项目作为子目录引入时，会自动创建 `robin_hood` INTERFACE 目标并暴露 `src/include/` 目录，因此不需要额外 wrapper。

## 更新步骤

1. 从上游仓库获取所需版本源码。
2. 将源码放入 `3rdparty/vendor/robin-hood-hashing/`。
3. 更新本文件中的版本号（以 `src/include/robin_hood.h` 中的宏定义为准）。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的版本号。
5. 重新配置并构建 klogg，确认编译通过。
