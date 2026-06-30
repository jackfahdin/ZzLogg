# KDSingleApplication 本地化处理说明

## 基本信息

- **库名称**：KDSingleApplication
- **来源提交**：`5b30db30266f92bc01f1439777803ce8dbf16c79`（短号 `5b30db3`）
- **上游仓库**：https://github.com/variar/KDSingleApplication
- **作用**：实现单实例应用程序策略，klogg 用它来确保同一台机器上同时只能运行一个 klogg 实例，并支持实例间消息传递。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 KDSingleApplication 源码放入 `3rdparty/vendor/KDSingleApplication/`，避免 CMake 配置时从 GitHub 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载，并手动创建 `kdsingleapp` 静态库：

```cmake
cpmaddpackage(
  NAME
  KDSingleApplication
  GITHUB_REPOSITORY
  variar/KDSingleApplication
  GIT_TAG
  5b30db30266f92bc01f1439777803ce8dbf16c79
  DOWNLOAD_ONLY
  YES
)
if(KDSingleApplication_ADDED)
  add_library(
    kdsingleapp STATIC ${KDSingleApplication_SOURCE_DIR}/src/kdsingleapplication.cpp
                     ${KDSingleApplication_SOURCE_DIR}/src/kdsingleapplication_localsocket.cpp
  )

  target_include_directories(kdsingleapp PUBLIC ${KDSingleApplication_SOURCE_DIR}/src)
  target_link_libraries(kdsingleapp Qt${QT_VERSION_MAJOR}::Core)
  target_compile_definitions(kdsingleapp PUBLIC KDSINGLEAPPLICATION_STATIC_BUILD)

  set_target_properties(kdsingleapp PROPERTIES AUTOMOC ON)
endif()
```

改为使用本地源码路径手动创建目标：

```cmake
# KDSingleApplication
add_library(
  kdsingleapp STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDSingleApplication/src/kdsingleapplication.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDSingleApplication/src/kdsingleapplication_localsocket.cpp
)
target_include_directories(
  kdsingleapp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDSingleApplication/src
)
target_link_libraries(kdsingleapp Qt${QT_VERSION_MAJOR}::Core)
target_compile_definitions(kdsingleapp PUBLIC KDSINGLEAPPLICATION_STATIC_BUILD)
set_target_properties(kdsingleapp PROPERTIES AUTOMOC ON)
```

保持使用 `kdsingleapp` 目标名，与 `src/app/CMakeLists.txt` 中的链接方式一致。

## 更新步骤

1. 从上游仓库获取目标提交（例如 `5b30db3`）。
2. 将源码放入 `3rdparty/vendor/KDSingleApplication/`。
3. 更新本文件中的来源提交 hash。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的 commit hash。
5. 重新配置并构建 klogg，验证单实例功能正常。
