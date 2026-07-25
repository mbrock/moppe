# nanoarrow

This directory vendors the Apache Arrow nanoarrow 0.8.0 C amalgamation and
the IPC extension used by Moppe's bundle storage.

The files were generated from the
`apache-arrow-nanoarrow-0.8.0` tag with:

```sh
python3 ci/scripts/bundle.py \
  --source-output-dir=dist \
  --include-output-dir=dist \
  --header-namespace= \
  --with-ipc \
  --with-flatcc
```

Moppe compiles:

- `nanoarrow.c` and `nanoarrow.h`: the core Arrow C Data implementation.
- `nanoarrow_ipc.c` and `nanoarrow_ipc.h`: Arrow IPC stream encoding and
  decoding.
- `flatcc.c` and `flatcc/*.h`: the FlatCC runtime required by the IPC
  extension.

The C++ convenience headers emitted by the bundler are intentionally omitted;
Moppe uses the stable C API directly. Apache and FlatCC license notices are
included alongside the sources.
