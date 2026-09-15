# Monocypher 离线依赖

- 版本：4.0.3，原始源文件未修改。
- 来源：https://monocypher.org/download/monocypher-4.0.3.tar.gz
- 官方摘要：https://monocypher.org/download/monocypher-4.0.3.tar.gz.sha512
- 引入日期：2026-09-15；引入前核对 https://monocypher.org/bugs。
- 用途：更新清单 Ed25519 验签，使用 optional 的 SHA-512 Ed25519 接口。
- 构建：两个 C 源文件静态编译，不联网下载，不引入 Qt。
- 许可证：采用随附 LICENCE.md 的 BSD-2-Clause 许可选项。

发布包 SHA-512（下载后已比对）：

```text
40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965
```

保留文件 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| LICENCE.md | 5f8360e4c06ddcc584bdb4b210c6af824c4bb301e6a9a521869b6d90795ca4b3 |
| src/monocypher.c | 57eb914fc88136119bd41655cccb8c250048bf54d470540625186f8ab16f64be |
| src/monocypher.h | c494da712122da7ff679fdcf318a5317e84972b6c950fe9d896212947797facd |
| src/optional/monocypher-ed25519.c | 60fce3578fb00b00da96490653d993c4cb427b1e1be38183285c66e04d22cc18 |
| src/optional/monocypher-ed25519.h | abc4fad381879f5c29176ebe014b9189956b3dfe0a3e36459b6990bc57212380 |
