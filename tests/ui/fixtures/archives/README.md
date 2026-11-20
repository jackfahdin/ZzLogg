# Independent archive fixtures

Run `python3 generate.py` only to regenerate these tiny fixtures. It uses Python
stdlib ZIP/tar encoders and an installed `7z` command (LZMA2, solid on/off, omitted
timestamps). C++ tests need neither generator dependency. Binary identities and
expected file bytes are recorded in `manifest.json`; encoder versions can change
compressed bytes while preserving the expected contents.

Files cover separate subdirectories, UTF-8 names/content, CRLF, NUL and an empty
file. The independent portable ZIP has an application, portable marker and
ownership manifest. It is a synthetic package, not an executable release.

The test executable also provides `--stream-probe INPUT OUTPUT` for the explicit
`scripts/dev/audit_compressed_streams.py` audit. That audit compares whole decoded
contents, preserves failing reports, and exits nonzero on compatibility gaps.
For arbitrary trailing bytes it applies a strict rejection goal; a format tool's
more permissive trailing-data policy is not automatically a format violation.
