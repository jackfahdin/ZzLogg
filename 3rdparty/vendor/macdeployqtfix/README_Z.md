# macdeployqtfix 本地化处理说明

## 基本信息

- **工具名称**：macdeployqtfix
- **来源提交**：`df888505849d3c06d20a4338af276dfa7d11826a`（短号 `df88850`）
- **上游仓库**：https://github.com/arl/macdeployqtfix
- **作用**：在 macOS 上配合 `macdeployqt` 完成 Qt 应用打包后的依赖补齐、权限修正和 rpath 修复。

## 本次本地化的修改

### 1. 本地存放 Python 脚本

将上游仓库中的 `macdeployqtfix.py` 直接放入 `3rdparty/vendor/macdeployqtfix/`，连同 `README.md` 和许可证文件一起纳入 git 管理。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载：

```cmake
cpmaddpackage(
  NAME
  macdeployqtfix
  GITHUB_REPOSITORY
  arl/macdeployqtfix
  GIT_TAG
  df888505849d3c06d20a4338af276dfa7d11826a
  DOWNLOAD_ONLY
  YES
)
if(macdeployqtfix_ADDED)
  configure_file(${macdeployqtfix_SOURCE_DIR}/macdeployqtfix.py ${CMAKE_BINARY_DIR}/macdeployqtfix.py COPYONLY)
endif()
```

改为直接使用本地文件：

```cmake
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/macdeployqtfix/macdeployqtfix.py
  ${CMAKE_BINARY_DIR}/macdeployqtfix.py
  COPYONLY
)
```

由于该仓库仅包含一个 Python 脚本，不是 CMake 项目，因此不需要 `add_subdirectory`，只需在 Apple 平台下复制脚本到构建目录即可。

## 更新步骤

1. 从上游仓库获取目标提交（例如 `df88850`）。
2. 将 `macdeployqtfix.py` 复制到本目录，覆盖旧文件。
3. 更新本文件中的来源提交 hash。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的 commit hash。
5. 在 macOS 上重新构建打包流程，验证脚本仍能正确修复 rpath。
