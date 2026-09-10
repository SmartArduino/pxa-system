#!/usr/bin/env bash
set -euo pipefail

# Resolve a complete WASI SDK without making the product repository own a
# machine-global toolchain. The pinned release stays outside version control.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
wasi_version="29.0"
cache_dir="${PXA_WASI_SDK_CACHE_DIR:-$pxa_system_dir/.pxa}"
auto_download="${PXA_WASI_SDK_AUTO_DOWNLOAD:-1}"

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

is_complete_sdk() {
    local candidate="$1"
    [[ -x "$candidate/bin/clang" ]] &&
        [[ -x "$candidate/bin/clang++" ]] &&
        [[ -x "$candidate/bin/llvm-ar" ]] &&
        [[ -x "$candidate/bin/llvm-ranlib" ]] &&
        [[ -d "$candidate/share/wasi-sysroot" ]]
}

emit_sdk() {
    realpath -m -- "$1"
    exit 0
}

for variable_name in PXA_WASI_SDK_DIR WASI_SDK_DIR WASI_SDK_PATH; do
    candidate="${!variable_name:-}"
    [[ -n "$candidate" ]] || continue
    if is_complete_sdk "$candidate"; then
        emit_sdk "$candidate"
    fi
    fail "$variable_name is not a complete WASI SDK: $candidate"
done

for candidate in /opt/wasi-sdk "/opt/wasi-sdk-$wasi_version"; do
    if is_complete_sdk "$candidate"; then
        emit_sdk "$candidate"
    fi
done

case "$(uname -s)" in
    Linux) host_os="linux" ;;
    *) fail "Automatic WASI SDK download supports Linux only; set WASI_SDK_DIR" ;;
esac
case "$(uname -m)" in
    x86_64|amd64)
        host_arch="x86_64"
        archive_sha256="87d1d1a2879d139cdc624b968efad3d4a97b8078cdff95e63ac88ecafd1a0171"
        ;;
    aarch64|arm64)
        host_arch="arm64"
        archive_sha256="052ad773397dc9e5aa99fb4cfef694175e6b1e81bb2ad1d3c8e7b3fc81441b7c"
        ;;
    *) fail "Automatic WASI SDK download supports x86_64 and arm64 hosts; set WASI_SDK_DIR" ;;
esac

sdk_name="wasi-sdk-$wasi_version-$host_arch-$host_os"
sdk_dir="$cache_dir/$sdk_name"
if is_complete_sdk "$sdk_dir"; then
    emit_sdk "$sdk_dir"
fi
if [[ -e "$sdk_dir" ]]; then
    fail "Cached WASI SDK is incomplete: $sdk_dir"
fi

if [[ "$auto_download" != "1" ]]; then
    fail "WASI SDK is unavailable; set WASI_SDK_DIR or enable PXA_WASI_SDK_AUTO_DOWNLOAD"
fi
command -v curl >/dev/null 2>&1 ||
    fail "curl is required to download WASI SDK; set WASI_SDK_DIR instead"
command -v sha256sum >/dev/null 2>&1 ||
    fail "sha256sum is required to verify WASI SDK; set WASI_SDK_DIR instead"
command -v tar >/dev/null 2>&1 ||
    fail "tar is required to unpack WASI SDK; set WASI_SDK_DIR instead"

mkdir -p "$cache_dir"
temporary_dir="$(mktemp -d "$cache_dir/.wasi-sdk-download.XXXXXX")"
trap 'rm -rf "$temporary_dir"' EXIT
archive="$temporary_dir/$sdk_name.tar.gz"
url="https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-29/$sdk_name.tar.gz"

printf 'Downloading %s into %s\n' "$sdk_name" "$cache_dir" >&2
curl --fail --location --retry 3 --output "$archive" "$url"
printf '%s  %s\n' "$archive_sha256" "$archive" | sha256sum --check --status ||
    fail "WASI SDK checksum verification failed: $url"
tar -xzf "$archive" -C "$temporary_dir"
is_complete_sdk "$temporary_dir/$sdk_name" ||
    fail "Downloaded archive does not contain a complete WASI SDK: $url"

# Rename is atomic within the cache directory. Another build may have won the
# race while this process downloaded; either complete cache is valid.
if ! mv -T "$temporary_dir/$sdk_name" "$sdk_dir" 2>/dev/null; then
    is_complete_sdk "$sdk_dir" ||
        fail "Unable to install WASI SDK into $sdk_dir"
fi
emit_sdk "$sdk_dir"
