# KArchive comparison host

This standalone project reuses the production Decompressor and preparePackage
sources and their four existing contract tests. It is not included by the default
build and never downloads dependencies. The only stub suppresses application log
emission; decoding, file writes, asynchronous completion, and package validation
remain real. Fork and upstream builds use the same host and test expectations.

The recorded candidate is the unmodified official KArchive 6.31.0 source archive:

- URL: https://download.kde.org/stable/frameworks/6.31/karchive-6.31.0.tar.xz
- SHA-256: `95acaf5a459593184104e1c157cef415ec59a33e71674186169e0df33cdacaac`

From the repository root, set `QT_PREFIX` to your Qt 6.10+ installation. Download
and extract that archive to `out/benchmarks/p13-karchive-evaluation/source/`,
verify its SHA-256, then run:

```sh
repo_root="$PWD"
evaluation_root="$repo_root/out/benchmarks/p13-karchive-evaluation"
cmake -S 3rdparty/vendor/extra-cmake-modules -B "$evaluation_root/build-ecm" -G Ninja \
  -DBUILD_TESTING=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF \
  -DCMAKE_INSTALL_PREFIX="$evaluation_root/ecm"
cmake --install "$evaluation_root/build-ecm"

cmake -S tests/experiments/karchive -B "$evaluation_root/build-fork" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DARCHIVE_BACKEND=fork
cmake --build "$evaluation_root/build-fork" -j6
ctest --test-dir "$evaluation_root/build-fork" --output-on-failure

cmake -S tests/experiments/karchive -B "$evaluation_root/build-upstream" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DARCHIVE_BACKEND=upstream \
  -DKARCHIVE_SOURCE="$evaluation_root/source/karchive-6.31.0" \
  -DECM_DIR="$evaluation_root/ecm/share/ECM/cmake" \
  -DWITH_OPENSSL=OFF -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE \
  -DWITH_LIBZSTD=OFF -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE
cmake --build "$evaluation_root/build-upstream" -j6
ctest --test-dir "$evaluation_root/build-upstream" --output-on-failure

python3 scripts/dev/audit_compressed_streams.py \
  --binary "$evaluation_root/build-upstream/bin/zzlogg_archive_test" \
  --output "$evaluation_root/audit-upstream"
```

Use a fresh/empty audit output directory on each run. Do not suppress nonzero
exit codes: upstream is expected to fail existing contracts until its gaps are
fixed. In the recorded run the fork passed 4/4 suites and 16/16 stream cases;
upstream passed 1/4 suites and 6/16 stream cases.

KArchive alone is static in both builds. The fork uses its embedded codecs;
upstream uses the installed zlib/bzip2/liblzma. This is a candidate-stack
compatibility comparison, **not** an isolated codec benchmark or a package-size
comparison. Upstream is compiled as C++20; the consumers remain C++17.

To inspect upstream defaults, use a separate build directory and omit all four
OpenSSL/Zstd disabling arguments. `WITH_*=OFF` alone does not disable discovery.
The minimal run deliberately disables PkgConfig discovery to omit zstd; this is
an isolated probe configuration, not a recommended global application setting.
The syntax-highlighting build's minimal ECMConfig shim is insufficient for
KArchive; use the full ECM installation above.

See [evaluation and limits](../../../docs/superpowers/plans/2026-11-20-karchive-evaluation.md).
