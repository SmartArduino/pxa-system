#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-spec-test.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

ruby -c "$script_dir/generate_golden.rb"
ruby -c "$script_dir/generate_package_golden.rb"
ruby -c "$script_dir/check_spec.rb"
ruby "$script_dir/check_spec.rb"
ruby "$script_dir/generate_golden.rb" "$work_dir/core-vectors.json" >/dev/null
ruby "$script_dir/generate_package_golden.rb" "$work_dir/package-vectors.json" >/dev/null
cmp "$work_dir/core-vectors.json" "$script_dir/../golden/core-vectors.json"
cmp "$work_dir/package-vectors.json" "$script_dir/../golden/package-vectors.json"
