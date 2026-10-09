#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <empty-output-directory>" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
output_dir="$(realpath -m -- "$1")"
if [[ -d "$output_dir" ]] && [[ -n "$(ls -A "$output_dir")" ]]; then
  echo "SDK output directory must be empty: $output_dir" >&2
  exit 2
fi

wamrc_bin="${WAMRC:-}"
if [[ -z "$wamrc_bin" ]]; then
  wamrc_bin="$("$script_dir/build_wamrc.sh")"
fi
if [[ ! -x "$wamrc_bin" ]]; then
  echo "WAMRC is not executable: $wamrc_bin" >&2
  exit 1
fi

mkdir -p "$output_dir/sdk" "$output_dir/tools/package" \
  "$output_dir/tools/wamr" "$output_dir/tools/i18n" \
  "$output_dir/spec/draft/tools" "$output_dir/spec/draft/golden" \
  "$output_dir/config" "$output_dir/bin"
cp -R "$pxa_system_dir/sdk/guest-cpp" "$output_dir/sdk/"
cp -R "$pxa_system_dir/sdk/guest-c" "$output_dir/sdk/"
cp -R "$pxa_system_dir/sdk/cmake" "$output_dir/sdk/"
for tool in package_app.sh resolve_wasi_sdk.sh \
  verify_wasm_core_imports.py verify_wasm_memory.py \
  build_package_manifest.py build_pxa_container.py \
  build_pxa_provenance.py compile_resources.py protocol_metadata.py; do
  cp "$script_dir/$tool" "$output_dir/tools/package/$tool"
done
cp "$pxa_system_dir/tools/wamr/metadata.py" "$output_dir/tools/wamr/"
cp "$pxa_system_dir/tools/i18n/compile_catalog.py" "$output_dir/tools/i18n/"
cp "$pxa_system_dir"/spec/draft/pxa-*.json "$output_dir/spec/draft/"
cp "$pxa_system_dir/spec/draft/tools/generate_service_codecs.py" \
  "$output_dir/spec/draft/tools/"
cp "$pxa_system_dir/spec/draft/golden/device-runtime-info.json" \
  "$output_dir/spec/draft/golden/"
cp "$pxa_system_dir/config/wamr.json" "$output_dir/config/"
cp "$pxa_system_dir/config/host-abi.json" "$output_dir/config/"
mkdir -p "$output_dir/tools/release"
cp "$pxa_system_dir/tools/release/metadata.py" "$output_dir/tools/release/"
cp "$pxa_system_dir/spec/draft/abi-1.0-envelope.json" "$output_dir/spec/draft/"
cp "$wamrc_bin" "$output_dir/bin/wamrc"
cp "$pxa_system_dir/VERSION" "$output_dir/VERSION"
cp "$pxa_system_dir/LICENSE" "$output_dir/LICENSE"
mkdir -p "$output_dir/docs" "$output_dir/licenses"
cp "$pxa_system_dir/docs/DEVKIT.zh-CN.md" "$pxa_system_dir/docs/VERSIONING.zh-CN.md" "$output_dir/docs/"
cp "$pxa_system_dir/wamr/LICENSE" "$output_dir/licenses/WAMR.txt"
cat > "$output_dir/README.md" <<'EOF'
# PXA C/C++ SDK

This SDK contains Guest headers, runtime sources, CMake modules and the PXA AOT compiler.
For a fully offline supported App workflow, use the matching Linux DevKit, which also
contains WASI SDK, Python, CMake/Ninja, signing tools and the simulator.
This smaller source bundle requires those external tools. It contains no development
private key. VERSION identifies the SDK release; package Core and service versions
remain independent. See docs/DEVKIT.zh-CN.md and docs/VERSIONING.zh-CN.md.
EOF

echo "PXA C/C++ SDK bundle: $output_dir"
