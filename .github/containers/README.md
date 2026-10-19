# 容器定义

`ubuntu24.04/Dockerfile` 是保留的历史构建环境定义。当前 Linux CI 直接使用 GitHub 托管 runner，见 [CI Build](../workflows/ci-build.yml)，不使用此镜像。

该 Dockerfile 中的 Qt 和 CMake 版本不满足当前项目要求；重新启用前需要按[构建指南](../../docs/development/BUILD.md)更新。目录迁移仅整理文件位置，不改变镜像内容。
