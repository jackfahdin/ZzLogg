# uchardet 本地化处理说明

## 基本信息

- **库名称**：uchardet
- **版本**：0.0.8
- **上游仓库**：https://gitlab.freedesktop.org/uchardet/uchardet
- **作用**：通用字符编码检测库，klogg 中用于自动识别日志文件的文本编码。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 uchardet 完整源码放入 `3rdparty/vendor/uchardet/`，避免 CMake 配置时从 GitLab 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载：

```cmake
cpmaddpackage(
  NAME
  Uchardet
  GIT_REPOSITORY
  https://gitlab.freedesktop.org/uchardet/uchardet
  VERSION
  0.0.8
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_BINARY OFF"
)
if(Uchardet_ADDED)
  message("Adding alias for uchardet")
  add_library(klogg_uchardet_wrapper INTERFACE)
  target_link_libraries(klogg_uchardet_wrapper INTERFACE libuchardet)
  target_include_directories(klogg_uchardet_wrapper INTERFACE ${Uchardet_SOURCE_DIR}/src)
else()
  add_library(klogg_uchardet_wrapper INTERFACE)
  target_link_libraries(klogg_uchardet_wrapper INTERFACE ${UCHARDET_LIBRARY})
  target_include_directories(klogg_uchardet_wrapper INTERFACE ${UCHARDET_INCLUDE_DIR})
endif()
```

改为本地 `add_subdirectory`：

```cmake
set(BUILD_BINARY OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/uchardet)
message("Adding alias for uchardet")
add_library(klogg_uchardet_wrapper INTERFACE)
target_link_libraries(klogg_uchardet_wrapper INTERFACE libuchardet)
target_include_directories(klogg_uchardet_wrapper INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/uchardet/src)
```

同时去掉了系统包回退分支，统一使用本地版本。

### 3. 构建选项

- `BUILD_BINARY OFF`：不构建命令行工具
- `BUILD_SHARED_LIBS OFF`：构建静态库，与 klogg 整体保持一致

## 更新步骤

1. 从上游仓库获取 `0.0.8` 版本源码。
2. 将源码放入 `3rdparty/vendor/uchardet/`。
3. 更新本文件中的版本号。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的版本号。
5. 重新配置并构建 klogg，验证编码自动检测功能正常。
