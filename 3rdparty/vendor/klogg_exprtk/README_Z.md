# klogg_exprtk 本地化处理说明

## 基本信息

- **库名称**：klogg_exprtk
- **来源提交**：`1f9f4cd7d2620b7b24232de9ea22908d63913459`（短号 `1f9f4cd`）
- **上游仓库**：https://github.com/variar/klogg_exprtk
- **作用**：exprtk 的 klogg 定制 fork，提供 C++ 数学表达式解析和求值，klogg 中用于高亮规则等需要表达式计算的场景。

## 本次本地化的修改

### 1. 本地存放完整源码仓库

将 klogg_exprtk fork 的完整源码放入 `3rdparty/vendor/klogg_exprtk/`，避免 CMake 配置时从 GitHub 下载。该库为 header-only，核心文件是 `exprtk.hpp`。

### 2. 修改 `3rdparty/CMakeLists.txt`

原本通过 CPM 下载：

```cmake
cpmaddpackage(
  NAME
  exprtk
  GITHUB_REPOSITORY
  variar/klogg_exprtk
  GIT_TAG
  1f9f4cd7d2620b7b24232de9ea22908d63913459
  DOWNLOAD_ONLY
  YES
)
if(exprtk_ADDED)
  add_library(exprtk INTERFACE)
  target_link_libraries(exprtk INTERFACE robin_hood)
  target_include_directories(exprtk SYSTEM INTERFACE ${exprtk_SOURCE_DIR})
  target_compile_definitions(
    exprtk
    INTERFACE -Dexprtk_disable_caseinsensitivity
              -Dexprtk_disable_comments
              -Dexprtk_disable_break_continue
              -Dexprtk_disable_return_statement
              -Dexprtk_disable_superscalar_unroll
              -Dexprtk_disable_rtl_io_file
              -Dexprtk_disable_rtl_vecops
              -Dexprtk_disable_string_capabilities
  )
endif()
```

改为直接使用本地路径创建 `exprtk` INTERFACE 目标：

```cmake
# exprtk
add_library(exprtk INTERFACE)
target_link_libraries(exprtk INTERFACE robin_hood)
target_include_directories(exprtk SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/klogg_exprtk)
target_compile_definitions(
  exprtk
  INTERFACE -Dexprtk_disable_caseinsensitivity
            -Dexprtk_disable_comments
            -Dexprtk_disable_break_continue
            -Dexprtk_disable_return_statement
            -Dexprtk_disable_superscalar_unroll
            -Dexprtk_disable_rtl_io_file
            -Dexprtk_disable_rtl_vecops
            -Dexprtk_disable_string_capabilities
)
```

## 更新步骤

1. 从上游 fork 获取目标提交（例如 `1f9f4cd`）。
2. 将源码放入 `3rdparty/vendor/klogg_exprtk/`。
3. 更新本文件中的来源提交 hash。
4. 更新 `3rdparty/vendor/README.md` 总览表格中的 commit hash。
5. 重新配置并构建 klogg，确认表达式相关功能正常。
