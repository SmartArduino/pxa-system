# PXA Specifications

[简体中文](README.zh-CN.md)

This directory contains one consolidated, intentionally unstable
[current draft](draft/README.md). Services retain their own protocol versions;
the directory name is not a compatibility version.

The `draft/pxa-*.json` files are the machine-readable sources of truth for
numeric assignments and protocol versions. Prose documents explain semantics
but must not assign different wire values. Run the dependency-free Python
checks from the repository root:

```sh
python3 spec/draft/tools/test.py
python3 tools/package/test_ui_vectors.py
```

The draft currently specifies Core 0.1, Window 0.1, UI 0.3, Network 0.2,
Audio 0.2, Package Manifest 0.6, Container 0.1 and the remaining published
services at 0.1.
