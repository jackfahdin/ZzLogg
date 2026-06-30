# KDToolBox

- **版本/提交**: `6468867d1a46eabe1bcb2cd342f338fe66f06675`
- **来源**: https://github.com/KDAB/KDToolBox
- **许可证**: MIT（见仓库根目录 `LICENSE.txt`）

## 说明

klogg 仅使用 KDToolBox 中的 `KDSignalThrottler` 组件（位于 `qt/KDSignalThrottler/src/`）。

本地仓库保留完整源码，但 CMake 中仅编译该组件：

```cmake
add_library(
  kdtoolbox STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDToolBox/qt/KDSignalThrottler/src/KDSignalThrottler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDToolBox/qt/KDSignalThrottler/src/KDSignalThrottler.h
)
target_include_directories(kdtoolbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/KDToolBox/qt/KDSignalThrottler/src)
target_link_libraries(kdtoolbox Qt${QT_VERSION_MAJOR}::Core)
set_target_properties(kdtoolbox PROPERTIES AUTOMOC ON)
```

## 修改

- 无源码修改。
- `3rdparty/CMakeLists.txt` 中原 `cpmaddpackage(KDAB/KDToolBox ...)` 已替换为上述本地静态库定义。
