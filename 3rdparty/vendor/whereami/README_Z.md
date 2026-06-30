# whereami 本地化处理说明

## 基本信息

- **库名称**：whereami
- **来源提交**：`dcb52a058dc14530ba9ae05e4339bd3ddfae0e0e`（短号 `dcb52a0`）
- **上游仓库**：https://github.com/gpakosz/whereami
- **作用**：跨平台获取当前可执行文件路径和模块路径，klogg 中用于定位程序所在目录。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 whereami 作为 git submodule 放入 `3rdparty/vendor/whereami/`，避免 CMake 配置时从 GitHub 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载：

```cmake
cpmaddpackage(
  NAME
  whereami
  GITHUB_REPOSITORY
  gpakosz/whereami
  GIT_TAG
  dcb52a058dc14530ba9ae05e4339bd3ddfae0e0e
  DOWNLOAD_ONLY
  YES
)
if(whereami_ADDED)
  add_library(whereami STATIC ${whereami_SOURCE_DIR}/src/whereami.h ${whereami_SOURCE_DIR}/src/whereami.c)
  target_include_directories(whereami PUBLIC ${whereami_SOURCE_DIR}/src)
endif()
```

改为使用本地源码路径创建静态库：

```cmake
# whereami
add_library(
  whereami STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whereami/src/whereami.h
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whereami/src/whereami.c
)
target_include_directories(whereami PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whereami/src)
```

## 更新步骤

1. 从上游仓库获取目标提交（例如 `dcb52a0`）。
2. 更新 submodule 或覆盖 `3rdparty/vendor/whereami/` 目录。
3. 更新本文件中的来源提交 hash。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的 commit hash。
5. 重新配置并构建 klogg，验证路径获取功能正常。
