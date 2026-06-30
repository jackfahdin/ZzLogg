# Catch2 本地化处理说明

## 基本信息

- **库名称**：Catch2
- **版本**：2.13.8
- **上游仓库**：https://github.com/catchorg/Catch2
- **作用**：C++ 单元测试框架，klogg 的单元测试和 UI 测试都基于它。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 Catch2 v2.13.8 完整源码放入 `3rdparty/vendor/Catch2/`，避免 CMake 配置时从 GitHub 下载。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载：

```cmake
cpmaddpackage(
  NAME
  Catch2
  GITHUB_REPOSITORY
  catchorg/Catch2
  VERSION
  2.13.8
  EXCLUDE_FROM_ALL
  YES
)
if(NOT TARGET Catch2)
  message("Adding imported target for catch2")
  add_library(Catch2 INTERFACE)
  target_include_directories(Catch2 INTERFACE ${CATCH2_INCLUDE_DIRS})
endif(NOT TARGET Catch2)
```

改为本地 `add_subdirectory`：

```cmake
# Catch2
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/Catch2)
```

Catch2 v2 的 `CMakeLists.txt` 作为子目录引入时会创建 `Catch2` INTERFACE 目标，klogg 的测试目标可直接通过 `target_link_libraries(... Catch2)` 链接。

### 3. 强制关闭自测试

通过 `CATCH_BUILD_TESTING OFF` 避免 Catch2 构建自身的 SelfTest 项目。

## 更新步骤

1. 从上游仓库获取 `v2.13.8` 标签源码。
2. 将源码放入 `3rdparty/vendor/Catch2/`。
3. 更新本文件中的版本号。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的版本号。
5. 重新配置并构建 klogg 测试，验证测试能正常编译和运行。
