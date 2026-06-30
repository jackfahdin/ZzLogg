# sentry-native

- **版本/提交**: `a3d58622a807b9dda174cb9fc18fa0f98c89d043`
- **来源**: https://github.com/getsentry/sentry-native
- **许可证**: MIT（见仓库根目录 `LICENSE`）

## 说明

本地保留完整 sentry-native 源码仓库，包含其 git 子模块（`external/crashpad`、`external/breakpad`、`external/libunwindstack-ndk`、`external/third_party/lss` 以及 crashpad 自身的 `third_party/mini_chromium`、`third_party/zlib`、`third_party/lss`）。

仅在 `KLOGG_USE_SENTRY=ON` 时启用，CMake 中设置后端为 `crashpad` 并关闭示例：

```cmake
if(KLOGG_USE_SENTRY)
  set(SENTRY_BACKEND "crashpad" CACHE INTERNAL "" FORCE)
  set(SENTRY_TRANSPORT "none" CACHE INTERNAL "" FORCE)
  set(SENTRY_BUILD_EXAMPLES OFF CACHE INTERNAL "" FORCE)
  set(CRASHPAD_ENABLE_INSTALL OFF CACHE INTERNAL "" FORCE)
  set(CRASHPAD_ENABLE_INSTALL_DEV OFF CACHE INTERNAL "" FORCE)

  add_subdirectory(vendor/sentry-native)

  if(WIN32)
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "klogg_crashpad_handler.exe")
  else()
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "klogg_crashpad_handler")
  endif()
endif()
```

## 修改

- 无源码修改。
- 将嵌套的 `.git` 目录移除，使子模块内容作为普通文件纳入 klogg 仓库管理。
- `3rdparty/CMakeLists.txt` 中原 `cpmaddpackage(getsentry/sentry-native ...)` 已替换为上述本地 `add_subdirectory`。
