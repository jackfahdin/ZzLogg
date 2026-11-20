# Independent concatenated streams

`python3 generate.py` uses Python's gzip/bz2/lzma encoders offline. Tests consume
these committed inputs and literal payload files, without Python at build/run time.
The manifest records input SHA-256, expected payload order, and the first member
size for controlled read boundaries; null parts means failure is required.

Cases cover empty members, multiple input/output blocks, damaged later members,
XZ padding across blocks, and legacy LZMA. Arbitrary trailing bytes use the
project's strict rejection goal. Legacy LZMA concatenation is unsupported.
