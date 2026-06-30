# xxHash 本地化处理说明

## 基本信息

- **库名称**：xxHash
- **版本**：0.8.1（`xxhash.h` 中 `XXH_VERSION_MAJOR.MINOR.RELEASE` 定义）
- **上游仓库**：https://github.com/Cyan4973/xxHash
- **作用**：极高速的非加密哈希算法，klogg 中用于文件内容校验、快速摘要等场景。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 xxHash 完整源码放入 `3rdparty/vendor/xxHash/`，避免 CMake 配置时从 GitHub 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载，再使用非官方 CMake 构建：

```cmake
cpmaddpackage(
  NAME
  xxHash
  GITHUB_REPOSITORY
  Cyan4973/xxHash
  VERSION
  0.8.1
  DOWNLOAD_ONLY
  YES
)
if(xxHash_ADDED)
  set(XXHASH_BUILD_ENABLE_INLINE_API OFF)
  set(XXHASH_BUILD_XXHSUM OFF)
  add_subdirectory(${xxHash_SOURCE_DIR}/cmake_unofficial ${CMAKE_BINARY_DIR}/xxhash EXCLUDE_FROM_ALL)
endif()
```

改为本地 `add_subdirectory`：

```cmake
# xxHash
set(XXHASH_BUILD_ENABLE_INLINE_API OFF CACHE BOOL "" FORCE)
set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/xxHash/cmake_unofficial ${CMAKE_BINARY_DIR}/xxhash)
```

`cmake_unofficial/CMakeLists.txt` 会自动检测 bundled 模式，构建静态库 `xxhash` 目标。

## 更新步骤

1. 从上游仓库获取 `v0.8.1` 标签源码。
2. 将源码放入 `3rdparty/vendor/xxHash/`。
3. 更新本文件中的版本号（以 `xxhash.h` 中的宏定义为准）。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的版本号。
5. 重新配置并构建 klogg，确认编译通过。
