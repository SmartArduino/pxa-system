# PXA Specifications

This directory contains the PXA specifications. Versions under `draft-*` are
intentionally unstable until explicitly promoted to a stable ABI.

The current Core, Package and non-UI service baseline is
[draft-0.1](draft-0.1/README.md). The current UI service baseline is the
breaking [UI 0.3 Draft 0.2](draft-0.2/ui.md).

The source of truth for numeric Core ABI assignments is
`draft-0.1/pxa-core.yaml`; UI 0.3 assignments are defined by
`draft-0.2/pxa-ui.yaml`. Prose documents explain semantics but must not assign
different wire values. Run `draft-0.1/tools/check_spec.rb` and
`../tools/test_ui_v2_vectors.py` to validate the checked-in sources and golden
vectors.
