#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <app-directory> <esp32s3|esp32s31|simulator> <output-dir>" >&2
  echo "Optional: PXA_APP_DEFINES=NAME=VALUE,NAME2=VALUE2" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
app_name="$1"
package_target="$2"
output_dir="$3"
if [[ ! "$app_name" =~ ^[a-z][a-z0-9._-]{0,63}$ ]]; then
  echo "Invalid PXA App directory name: $app_name" >&2
  exit 2
fi
default_app_source_root="$pxa_system_dir/apps/pxa"
app_source_root="$(realpath -m -- "${PXA_APP_SOURCE_ROOT:-$default_app_source_root}")"
app_dir="$(realpath -m -- "$app_source_root/$app_name")"
if [[ "$app_dir" != "$app_source_root/$app_name" ]]; then
  echo "PXA App source must remain below its source root: $app_name" >&2
  exit 2
fi
clang_bin="${CLANG:-clang}"
cmake_bin="${CMAKE:-cmake}"
wamrc_bin="${WAMRC:-}"
private_key="${PXA_SIGNING_KEY:-$app_source_root/.dev-signing/publisher-private.pem}"
engine_abi="$("${PYTHON:-python3}" "$pxa_system_dir/tools/wamr/metadata.py" engine_abi)"
output_dir="$(realpath -m -- "$output_dir")"
default_output_root="$pxa_system_dir/out/packages"
extra_output_root="${PXA_PACKAGE_OUTPUT_ROOT:-}"
app_define_args=()
app_definitions=()
if [[ -n "${PXA_APP_DEFINES:-}" ]]; then
  IFS=',' read -r -a requested_defines <<< "$PXA_APP_DEFINES"
  for requested_define in "${requested_defines[@]}"; do
    if [[ ! "$requested_define" =~ ^[A-Za-z_][A-Za-z0-9_]*(=[0-9]+)?$ ]]; then
      echo "Invalid PXA App compile definition: $requested_define" >&2
      exit 2
    fi
    app_define_args+=("-D$requested_define")
    app_definitions+=("$requested_define")
  done
fi

output_allowed=0
for output_root in "$default_output_root" "$extra_output_root"; do
  [[ -n "$output_root" ]] || continue
  output_root="$(realpath -m -- "$output_root")"
  if [[ "$output_dir" == "$output_root/pxa-$app_name" ]]; then
    output_allowed=1
    break
  fi
done
if [[ "$output_allowed" -ne 1 ]]; then
  echo "Refusing PXA Package output outside an approved built-in root: $output_dir" >&2
  exit 2
fi

case "$package_target" in
  esp32s3)
    aot_target="xtensa"
    manifest_target="esp32-s3"
    # Select the actual ESP32-S3 instruction model and match the firmware's
    # windowed ABI. Keep the CPU explicit so a future LLVM default change
    # cannot silently switch packages to a generic or call0 configuration.
    # The board disables FreeRTOS trace, so WAMR cannot obtain a native stack
    # boundary for per-function checks.
    # Performance first: size-level 0 selects the LLVM large code model, which
    # is what WAMR's xtensa AOT loader wants for modules whose text exceeds the
    # l32r literal range (literal islands). The AOT is larger but the raster
    # loops keep their full optimization.
    wamrc_extra_args=(--cpu=esp32s3
                      --stack-bounds-checks=0
                      --opt-level=3 --size-level=0
                      --mllvm=-mtext-section-literals)
    ;;
  esp32s31)
    aot_target="riscv32"
    manifest_target="esp32-s31"
    # ESP32-S31 is RV32IMAF. Keep the ABI in step with the WAMR runtime's
    # RISCV32_ILP32F configuration selected by ESP-IDF 6.2.
    # LLVM's RISC-V backend does not support the large code model selected by
    # size-level 0, unlike the Xtensa target above.
    wamrc_extra_args=(--target-abi=ilp32f --cpu=generic-rv32
                      --cpu-features=+m,+a,+f --opt-level=3 --size-level=3)
    ;;
  simulator)
    aot_target="x86_64"
    manifest_target="linux-x86_64"
    wamrc_extra_args=()
    ;;
  *)
    echo "Unsupported PXA Package target: $package_target" >&2
    exit 2
    ;;
esac

if [[ ! -f "$app_dir/package.json" ]]; then
  echo "PXA App source is incomplete: $app_dir" >&2
  exit 1
fi
if [[ ! -f "$private_key" ]]; then
  echo "PXA signing key is missing: $private_key" >&2
  exit 1
fi
if [[ -z "$wamrc_bin" ]]; then
  wamrc_bin="$($script_dir/build_wamrc.sh)"
fi
if [[ ! -x "$wamrc_bin" ]]; then
  echo "wamrc is not executable: $wamrc_bin" >&2
  exit 1
fi

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-$app_name-$package_target.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
package_dir="$work_dir/package"
mkdir -p "$package_dir/artifacts"

generated_include_dir="$work_dir/generated/include"
generated_include_args=()
if [[ -f "$app_dir/i18n/messages.yaml" ]]; then
  mapfile -t locale_catalogs < <(
    find "$app_dir/i18n" -maxdepth 1 -type f -name '*.yaml' \
      ! -name 'messages.yaml' -print | LC_ALL=C sort
  )
  "${PYTHON:-python3}" \
    "$pxa_system_dir/tools/i18n/compile_catalog.py" \
    "$app_dir/i18n/messages.yaml" "${locale_catalogs[@]}" \
    --output "$generated_include_dir/pxa_app_messages.h"
  generated_include_args=("-I$generated_include_dir")
fi

component_rows="$work_dir/components.tsv"
build_settings="$work_dir/build.tsv"
"${PYTHON:-python3}" - "$app_dir/package.json" "$build_settings" > "$component_rows" <<'PYTHON'
import json
import sys

metadata = json.load(open(sys.argv[1], encoding="utf-8"))
build = metadata.get("build", {"system": "direct"})
if not isinstance(build, dict) or set(build) - {"system", "source_dir", "linear_memory"}:
    raise SystemExit(
        "build must be an object with system, optional source_dir, and optional linear_memory")
build_system = build.get("system", "direct")
if build_system not in ("direct", "cmake"):
    raise SystemExit("build system must be direct or cmake")
source_dir = build.get("source_dir", ".")
if not isinstance(source_dir, str) or not source_dir:
    raise SystemExit("build source_dir must be a non-empty string")
linear_memory = build.get("linear_memory")
linear_memory_maximum = 0
if linear_memory is not None:
    if build_system != "direct":
        raise SystemExit("build linear_memory is only supported by direct builds")
    if (not isinstance(linear_memory, dict) or
            set(linear_memory) != {"maximum_bytes", "pinned"}):
        raise SystemExit(
            "build linear_memory must contain maximum_bytes and pinned")
    linear_memory_maximum = linear_memory["maximum_bytes"]
    if (not isinstance(linear_memory_maximum, int) or
            isinstance(linear_memory_maximum, bool) or
            linear_memory_maximum < 65536 or
            linear_memory_maximum > 4294967296 or
            linear_memory_maximum % 65536 != 0):
        raise SystemExit(
            "build linear_memory maximum_bytes must be a WebAssembly page multiple")
    if linear_memory["pinned"] is not True:
        raise SystemExit("build linear_memory pinned must be true")
open(sys.argv[2], "w", encoding="utf-8").write(
    f"{build_system}\t{source_dir}\t{linear_memory_maximum}\n")
components = metadata.get("components", [{"id": "main", "source": "main.c"}])
if not isinstance(components, list) or not components:
    raise SystemExit("components must be a non-empty array")
for component in components:
    if not isinstance(component, dict):
        raise SystemExit("component must be an object")
    component_id = component.get("id")
    if not isinstance(component_id, str):
        raise SystemExit("component id is required")
    artifact = component.get("artifact", "both")
    if artifact not in ("aot", "wasm", "both"):
        raise SystemExit("component artifact must be aot, wasm, or both")
    if build_system == "cmake":
        target = component.get("cmake_target")
        if not isinstance(target, str) or not target:
            raise SystemExit("component cmake_target is required for a CMake build")
        if component.get("wasi") is None:
            raise SystemExit("CMake components must declare wasi metadata")
        print(f"{component_id}\t{artifact}\t{target}")
        continue
    if "source" in component and "sources" in component:
        raise SystemExit("component cannot contain both source and sources")
    sources = component.get("sources")
    if sources is None:
        sources = [component.get("source", "main.c" if component_id == "main" else None)]
    if not isinstance(sources, list) or not sources or not all(
            isinstance(source, str) and source for source in sources):
        raise SystemExit("component sources must be a non-empty string array")
    for source in sources:
        print(f"{component_id}\t{artifact}\t{source}")
PYTHON

IFS=$'\t' read -r build_system build_source_dir linear_memory_maximum \
  < "$build_settings"
component_ids=()
declare -A component_seen=()
declare -A component_value_map=()
declare -A component_artifact_map=()
while IFS=$'\t' read -r component_id component_artifact component_value; do
  if [[ ! "$component_id" =~ ^[a-z][a-z0-9._-]{0,63}$ ]]; then
    echo "Invalid PXA Component id: $component_id" >&2
    exit 1
  fi
  if [[ "$build_system" == "cmake" ]]; then
    if [[ -n "${component_seen[$component_id]+present}" ||
          ! "$component_value" =~ ^[A-Za-z0-9_.+-]+$ ]]; then
      echo "Invalid or duplicate PXA CMake target for Component: $component_id" >&2
      exit 1
    fi
  else
    source_path="$(realpath -m -- "$app_dir/$component_value")"
    if [[ "$source_path" != "$app_dir/"* || ! -f "$source_path" ]]; then
      echo "Invalid PXA Component source: $component_value" >&2
      exit 1
    fi
    component_value="$source_path"
  fi
  if [[ -z "${component_seen[$component_id]+present}" ]]; then
    component_ids+=("$component_id")
    component_value_map["$component_id"]="$component_value"
    component_artifact_map["$component_id"]="$component_artifact"
  else
    if [[ "${component_artifact_map[$component_id]}" != "$component_artifact" ]]; then
      echo "Inconsistent artifact selection for Component: $component_id" >&2
      exit 1
    fi
    component_value_map["$component_id"]+=$'\n'"$component_value"
  fi
  component_seen["$component_id"]=1
done < "$component_rows"
if [[ ${#component_ids[@]} -eq 0 ]]; then
  echo "PXA App source has no Components: $app_dir" >&2
  exit 1
fi

# A caller may seed additional architecture AOT files here to create a single
# multi-architecture Package. The current build targets add one architecture.
if [[ -n "${PXA_EXTRA_AOT_DIR:-}" ]]; then
  if [[ ! -d "$PXA_EXTRA_AOT_DIR" ]]; then
    echo "PXA_EXTRA_AOT_DIR is not a directory: $PXA_EXTRA_AOT_DIR" >&2
    exit 1
  fi
  for component_id in "${component_ids[@]}"; do
    for artifact in "$PXA_EXTRA_AOT_DIR"/"$component_id".*.aot; do
      [[ -f "$artifact" ]] || continue
      cp "$artifact" "$package_dir/artifacts/"
    done
  done
fi

if [[ "$build_system" == "cmake" ]]; then
  wasi_sdk_dir="$("$script_dir/resolve_wasi_sdk.sh")"
  cmake_source_dir="$(realpath -m -- "$app_dir/$build_source_dir")"
  if [[ "$cmake_source_dir" != "$app_dir" && "$cmake_source_dir" != "$app_dir/"* ]] ||
     [[ ! -f "$cmake_source_dir/CMakeLists.txt" ]]; then
    echo "Invalid PXA CMake source directory: $build_source_dir" >&2
    exit 1
  fi
  joined_definitions="$(IFS=';'; printf '%s' "${app_definitions[*]}")"
  "$cmake_bin" -S "$cmake_source_dir" -B "$work_dir/cmake-build" \
    -DCMAKE_TOOLCHAIN_FILE="$pxa_system_dir/sdk/cmake/pxa-wasi-toolchain.cmake" \
    -DWASI_SDK_DIR="$wasi_sdk_dir" \
    -DPXA_GUEST_SDK_DIR="$pxa_system_dir/sdk/guest-c" \
    -DPXA_GENERATED_INCLUDE_DIR="$generated_include_dir" \
    -DPXA_ARTIFACT_DIR="$package_dir/artifacts" \
    -DPXA_APP_DEFINITIONS="$joined_definitions" \
    -DPXA_CMAKE_MODULE_DIR="$pxa_system_dir/sdk/cmake"
  cmake_targets=()
  for component_id in "${component_ids[@]}"; do
    cmake_targets+=("${component_value_map[$component_id]}")
  done
  "$cmake_bin" --build "$work_dir/cmake-build" --target "${cmake_targets[@]}"
else
  linear_memory_link_args=()
  if [[ "$linear_memory_maximum" -ne 0 ]]; then
    linear_memory_link_args=("-Wl,--max-memory=$linear_memory_maximum")
  fi
  for component_id in "${component_ids[@]}"; do
    readarray -t component_sources <<< "${component_value_map[$component_id]}"
    "$clang_bin" --target=wasm32-unknown-unknown -O3 -fno-builtin -nostdlib \
      -I"$pxa_system_dir/sdk/guest-c/include" \
      -I"${PXA_APP_COMMON_DIR:-$app_source_root/common}" \
      "${generated_include_args[@]}" \
      "${app_define_args[@]}" \
      -Wl,--no-entry \
      -Wl,--allow-undefined-file="$pxa_system_dir/sdk/guest-c/pxa-imports.txt" \
      -Wl,--export=pxa_app_start \
      -Wl,--export=pxa_app_on_event -Wl,--export=pxa_app_stop \
      -Wl,--export=__heap_base -Wl,--export=__data_end \
      "${linear_memory_link_args[@]}" \
      "${component_sources[@]}" -o "$package_dir/artifacts/$component_id.wasm"
    if [[ "$linear_memory_maximum" -ne 0 ]]; then
      "${PYTHON:-python3}" "$script_dir/verify_wasm_memory.py" \
        "$package_dir/artifacts/$component_id.wasm" \
        --maximum-bytes "$linear_memory_maximum"
    fi
  done
fi

for component_id in "${component_ids[@]}"; do
  if [[ ! -f "$package_dir/artifacts/$component_id.wasm" ]]; then
    echo "PXA build did not produce artifacts/$component_id.wasm" >&2
    exit 1
  fi
  if [[ "${component_artifact_map[$component_id]}" != "wasm" ]]; then
    "$wamrc_bin" --target="$aot_target" "${wamrc_extra_args[@]}" \
      -o "$package_dir/artifacts/$component_id.$manifest_target.aot" \
      "$package_dir/artifacts/$component_id.wasm"
  fi
  if [[ "${component_artifact_map[$component_id]}" == "aot" ]]; then
    rm "$package_dir/artifacts/$component_id.wasm"
  fi
done

asset_dir="$app_dir/assets-$package_target"
if [[ ! -d "$asset_dir" ]]; then
  asset_dir="$app_dir/assets"
fi
if [[ -d "$asset_dir" ]]; then
  cp -R "$asset_dir" "$package_dir/assets"
fi
"${PYTHON:-python3}" "$script_dir/build_package_manifest.py" \
  "$app_dir/package.json" "$package_dir" "$private_key" \
  "$manifest_target" "$engine_abi"

mkdir -p "$output_dir"
find "$output_dir" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
cp -R "$package_dir"/. "$output_dir"/

container_output="${PXA_CONTAINER_OUTPUT:-${output_dir}.pxa}"
"${PYTHON:-python3}" "$script_dir/build_pxa_container.py" \
  "$package_dir" "$private_key" "$container_output" --codec lz4

provenance_output="${PXA_PROVENANCE_OUTPUT:-${container_output}.provenance.json}"
"${PYTHON:-python3}" "$script_dir/build_pxa_provenance.py" \
  "$package_dir" "$container_output" "$app_dir/package.json" \
  "$manifest_target" "$engine_abi" "$provenance_output"
