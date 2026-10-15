#!/usr/bin/env bash
set -euo pipefail

if [[ "${MSYSTEM:-}" != "MSYS" ]]; then
  echo "Open the MSYS2 MSYS shell and run this script there." >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -W)"
hev_dir="$repo_root/native/hev-socks5-tunnel"
vendor_dir="$repo_root/build/vendor/hev-source"

tag="$(git -C "$hev_dir" describe --exact-match --tags HEAD)"
if [[ "$tag" != "2.19.0" ]]; then
  echo "Expected the pinned HEV 2.19.0 tag; found $tag." >&2
  exit 1
fi

command -v gcc >/dev/null
command -v make >/dev/null
export MSYS=winsymlinks:native

make -C "$hev_dir" clean
make -C "$hev_dir" exec

mkdir -p "$vendor_dir"
if [[ -f "$hev_dir/bin/hev-socks5-tunnel.exe" ]]; then
  cp "$hev_dir/bin/hev-socks5-tunnel.exe" "$vendor_dir/hev-socks5-tunnel.exe"
elif [[ -f "$hev_dir/bin/hev-socks5-tunnel" ]]; then
  cp "$hev_dir/bin/hev-socks5-tunnel" "$vendor_dir/hev-socks5-tunnel.exe"
else
  echo "The official HEV build did not produce its executable." >&2
  exit 1
fi

official_runtime="$repo_root/build/vendor/hev-release/hev-socks5-tunnel/msys-2.0.dll"
if [[ ! -f "$official_runtime" ]]; then
  echo "Run tool/build_windows.ps1 once to fetch the pinned HEV runtime package." >&2
  exit 1
fi
cp "$official_runtime" "$vendor_dir/msys-2.0.dll"

if [[ ! -f "$repo_root/build/vendor/wintun-0.14.1/wintun/bin/amd64/wintun.dll" ]]; then
  echo "Run tool/build_windows.ps1 once to fetch the verified official Wintun package." >&2
  exit 1
fi
cp "$repo_root/build/vendor/wintun-0.14.1/wintun/bin/amd64/wintun.dll" \
   "$vendor_dir/wintun.dll"
cp "$repo_root/build/vendor/wintun-0.14.1/wintun/LICENSE.txt" \
   "$vendor_dir/LICENSE-Wintun.txt"

echo "Built HEV 2.19.0 from the pinned source submodule."
