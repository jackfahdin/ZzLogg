# 本地依赖包目录

本目录（`3rdparty/vendor/`）用于存放项目在受限网络环境下构建所需的第三方依赖本地副本。

## 本地化的依赖

| 目录 | 名称 | 版本 | 作用 | 详细说明 |
|------|------|------|------|----------|
| `simdutf/` | simdutf | 5.6.2 | 高性能 UTF 编码转换与校验 | [simdutf/README.md](simdutf/README.md) |
| `type_safe/` | type_safe | 0.2.4 | 强类型安全包装库 | [type_safe/README_Z.md](type_safe/README_Z.md) |
| `CRoaring/` | CRoaring | 4.2.1 | 压缩位图索引 | [CRoaring/README_Z.md](CRoaring/README_Z.md) |

## 注意事项
- 更新依赖时，需同步更新对应子目录的 `README.md` 和本总览文件中的版本号或者commit hash。
