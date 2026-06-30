# mimalloc

- **版本**: `2.1.7`
- **来源**: https://github.com/microsoft/mimalloc
- **许可证**: MIT（见仓库根目录 `LICENSE`）

## 说明

本地保留完整 mimalloc 源码仓库。klogg 通过 `add_subdirectory` 引入，仅构建静态库 `mimalloc-static`，并关闭测试、覆盖和动态库：

```cmake
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_SECURE OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/mimalloc)

add_library(klogg_mimalloc_wrapper INTERFACE)
target_link_libraries(klogg_mimalloc_wrapper INTERFACE mimalloc-static)
target_include_directories(klogg_mimalloc_wrapper INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/mimalloc/include)
```

## 修改

- 无源码修改。
- `3rdparty/CMakeLists.txt` 中原 `cpmaddpackage(microsoft/mimalloc ...)` 已替换为上述本地 `add_subdirectory`。
