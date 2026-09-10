# WAMR ESP-IDF Patch Series

The `series` file is the authoritative application order. Every patch applies
to the WAMR commit pinned in `config/wamr.json`. `tools/wamr/prepare_overlay.py`
verifies the commit and exact hunk context, writes patched files below the build
directory, and verifies that the submodule remains clean.

To update WAMR, first change the submodule gitlink, then rebase every patch onto
that commit. Run the verifier before changing `config/wamr.json`; update the
WAMR version and `engine_abi` together, regenerate all AOT packages, and record
the compatibility impact. Never edit or commit generated overlay files.

## 中文

`series` 是补丁应用顺序的唯一来源。所有补丁都基于
`config/wamr.json` 固定的 WAMR 提交。`tools/wamr/prepare_overlay.py` 会校验
提交和补丁上下文，只在构建目录生成覆盖源码，并再次确认子模块保持干净。

升级 WAMR 时，先更新子模块 gitlink，再把每个补丁 rebase 到新提交。修改
`config/wamr.json` 前必须运行校验器，并同步更新 WAMR 版本和 `engine_abi`、
重新生成全部 AOT 包、记录兼容性影响。不要修改或提交生成的 overlay 文件。
