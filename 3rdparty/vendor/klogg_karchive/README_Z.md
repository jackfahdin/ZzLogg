# klogg_karchive 本地化处理说明

## 基本信息

- **库名称**：klogg_karchive
- **来源提交**：`f546bf6ae66a8d34b43da5a41afcfbf4e1a47906`（短号 `f546bf6`）
- **上游仓库**：https://github.com/variar/klogg_karchive
- **作用**：KF5Archive 的 fork，内嵌 zlib、bzip2、lzma 实现，提供 Qt5/Qt6 兼容的压缩/解压缩支持。klogg 中用于解压压缩日志文件（如 `.zip`、`.tar.gz`、`.tar.bz2`、`.tar.xz` 等）。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 klogg_karchive fork 的完整源码放入 `3rdparty/vendor/klogg_karchive/`，避免 CMake 配置时从 GitHub 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载，并在未找到时回退到系统 `KF5Archive`：

```cmake
if(${QT_VERSION_MAJOR} EQUAL 6)
  set(CPM_USE_LOCAL_PACKAGES OFF)
endif()

cpmaddpackage(
  NAME
  KF5Archive
  GITHUB_REPOSITORY
  variar/klogg_karchive
  GIT_TAG
  f546bf6ae66a8d34b43da5a41afcfbf4e1a47906
  EXCLUDE_FROM_ALL
  YES
)
if(NOT KF5Archive_ADDED)
  find_package(KF5Archive)
  add_library(klogg_karchive INTERFACE)
  target_link_libraries(klogg_karchive INTERFACE KF5::Archive)
endif()

if(${QT_VERSION_MAJOR} EQUAL 6)
  message("Resetting CPM_USE_LOCAL_PACKAGES to ${_TMP_CPM_USE_LOCAL_PACKAGES}")
  set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})
endif()
```

改为直接使用本地版本：

```cmake
# klogg_karchive（KF5Archive 的 Qt5/Qt6 兼容 fork）
add_subdirectory(vendor/klogg_karchive)

if(${QT_VERSION_MAJOR} EQUAL 6)
  message("Resetting CPM_USE_LOCAL_PACKAGES to ${_TMP_CPM_USE_LOCAL_PACKAGES}")
  set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})
endif()
```

由于本地 fork 的 `CMakeLists.txt` 会直接创建 `klogg_karchive` 静态库目标，因此不再需要系统包回退分支。

### 3. 修复 Qt6 下 `QString::arg(QIODevice::OpenMode)` 编译错误

Qt6 中 `QIODevice::OpenMode` 无法直接传给 `QString::arg`，因此在以下位置显式转换为 `int`：

- `karchive/src/kar.cpp`
- `karchive/src/krcc.cpp`
- `karchive/src/karchive.cpp`

## 更新步骤

1. 从上游 fork 获取目标提交（例如 `f546bf6`）。
2. 将源码放入 `3rdparty/vendor/klogg_karchive/`。
3. 更新本文件中的来源提交 hash。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的 commit hash。
5. 重新配置并构建 klogg，验证压缩日志文件解压功能正常。
