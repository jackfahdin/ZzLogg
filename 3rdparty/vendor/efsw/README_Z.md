# efsw 本地化处理说明

## 基本信息

- **库名称**：efsw
- **版本**：1.4.1
- **上游仓库**：https://github.com/SpartanJ/efsw
- **作用**：跨平台文件系统监控库，klogg 中用于监听日志文件变化（如 tail 功能）。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 efsw 完整源码放入 `3rdparty/vendor/efsw/`，避免 CMake 配置时从 GitHub 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载：

```cmake
cpmaddpackage(
  NAME
  efsw
  GITHUB_REPOSITORY
  SpartanJ/efsw
  GIT_TAG
  1.4.1
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_TEST_APP OFF"
)
if(efsw_ADDED)
  target_compile_definitions(efsw PRIVATE EFSW_FSEVENTS_NOT_SUPPORTED)
endif()
```

改为本地 `add_subdirectory`：

```cmake
# efsw
set(BUILD_TEST_APP OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/efsw)
target_compile_definitions(efsw PRIVATE EFSW_FSEVENTS_NOT_SUPPORTED)
```

efsw 的 `CMakeLists.txt` 会创建 `efsw` 目标（同时还会创建 `efsw-static` 静态库目标）。klogg 的 `src/filewatch` 链接 `efsw`。

### 3. 编译定义

`EFSW_FSEVENTS_NOT_SUPPORTED`：在某些 macOS 环境或配置中禁用 FSEvents 后端，避免构建问题。

## 更新步骤

1. 从上游仓库获取 `v1.4.1` 标签源码。
2. 将源码放入 `3rdparty/vendor/efsw/`。
3. 更新本文件中的版本号。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的版本号。
5. 重新配置并构建 klogg，验证文件监控功能正常。
