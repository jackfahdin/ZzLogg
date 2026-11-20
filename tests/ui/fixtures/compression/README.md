# Independent stream fixtures

Generated offline by `python3 generate.py`, using Python's standard-library gzip,
bzip2 and xz encoders, independently of the vendored decoders. Tests consume these
small committed files directly; Python is not a C++ build or test requirement.

The decoded data is empty, one UTF-8 Android-style log line followed by NUL and
`tail\n`, or that same byte sequence repeated/truncated to 4 MiB + 73 bytes.
`manifest.json` records uncompressed sizes/hashes and the compressed file hashes.
Encoder versions can change the compressed bytes without changing their meaning.
Corruption and truncation are applied to temporary copies by the C++ test.
