# Archive output boundary fixtures

Run `python3 generate.py` offline. Python zipfile/tarfile write independent
archives; tests need neither Python nor external commands. Manifest SHA-256
identifies every input. All tests use owned temporary output and sibling sentinel
directories, including the deliberately escaping relative paths.

Cases include parent traversal, a sibling sharing the extraction root prefix,
backslash paths, external and indirect link targets, and valid internal links
(including a directory link and a chain). Malformed tar sizes have valid header
checksums so the test reaches size validation. No huge payload is stored or
extracted: integer-overflow/cancellation checks use metadata-only entries.

Mixed-case and canonically equivalent Unicode link spellings are rejected before
output, as are output names that collide after NFC normalization and case folding.
These are intentionally portable rejection rules, including on case-sensitive
Linux; running these fixtures there does not constitute native macOS/Windows
filesystem validation. Exact-spelling internal links remain supported.
