# oneTBB

- **版本/提交**: `c9be1ac2930f02dea523003ed801b4489f3e6b6e`（基于 oneTBB 2021.13.0）
- **来源**: https://github.com/variar/oneTBB
- **许可证**: Apache-2.0（见仓库根目录 `LICENSE.txt`）

## 说明

本地保留完整 oneTBB 源码仓库。klogg 通过 `add_subdirectory` 引入，需要的目标包括 `tbb`、`tbbmalloc` 和 `tbbmalloc_proxy`。

CMake 中关闭测试、示例并放宽严格警告：

```cmake
set(TBB_TEST OFF CACHE BOOL "" FORCE)
set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TBB_STRICT OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/oneTBB)
```

## 修改

- 无源码修改。
- `3rdparty/CMakeLists.txt` 中原 `cpmaddpackage(variar/oneTBB ...)` 已替换为上述本地 `add_subdirectory`。
