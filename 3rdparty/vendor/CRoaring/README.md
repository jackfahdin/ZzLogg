# CRoaring 本地化处理说明

## 基本信息

- **库名称**：CRoaring
- **版本**：4.2.1
- **上游仓库**：https://github.com/RoaringBitmap/CRoaring
- **对应标签**：`v4.2.1`
- **作用**：提供压缩位图（Roaring Bitmap）的 C/C++ 实现，klogg 中用于高效存储和操作行号集合（如搜索结果、过滤结果等）。

## 本次本地化的修改

### 1. 改用 amalgamation 分发形式

没有引入 CRoaring 完整源码仓库，而是直接使用了上游 release 提供的合并后文件：

- `roaring.c` —— C API 实现
- `roaring.h` —— C API 头文件
- `roaring.hh` —— C++ API 头文件

### 2. 新增兼容 shim

上游 amalgamation 已将 `Roaring64Map` 合并进 `roaring.hh`，不再单独提供 `roaring64map.hh`。为了兼容项目中已有的 `#include <roaring64map.hh>`，新增了一个本地 shim：

```cpp
#pragma once

#include "roaring.hh"
```

### 3. 新增 `CMakeLists.txt`

绕开 CRoaring 原本复杂的 CMake 构建系统，直接创建以下目标：

- `roaring` —— 静态库，编译 `roaring.c`
- `roaring-headers` —— C API header-only 目标
- `roaring-headers-cpp` —— C++ API header-only 目标

这些目标名称与项目原有链接方式保持一致。
