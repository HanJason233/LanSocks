# Third-party notices

## HEV SOCKS5 Tunnel 2.19.0

The Windows data-plane executable is from the official HEV 2.19.0 release. Its source is pinned in `native/hev-socks5-tunnel/`; the project license is in that submodule's `LICENSE` file. The Windows bundle includes a copy as `data/LICENSE-HEV.txt`.

## Wintun 0.14.1

LanSocks ships the official Wintun prebuilt `wintun.dll` beside HEV, which uses Wintun through its published API. The Wintun prebuilt-binary license is `data/LICENSE-Wintun.txt` in a build output. Upstream distribution: <https://www.wintun.net/builds/>.

## MSYS2 runtime

The official HEV Windows release includes `msys-2.0.dll` (file version 3.6.9). MSYS2 identifies `msys2-runtime` as GPL-3.0-or-later. The build script downloads the official MSYS2 3.6.9-1 source archive, verifies its SHA-256, and places it in `third_party_sources/` next to the Windows bundle. The archive contains the corresponding source package and license texts.

- Source archive: <https://repo.msys2.org/msys/sources/msys2-runtime-3.6.9-1.src.tar.zst>
- SHA-256: `bff64ab3d9386b0c29754951e7c121f85c30c846258e45198437940b00621ac8`
- Upstream project: <https://github.com/msys2/msys2-runtime>

The HEV, MSYS2, and Wintun components retain their respective upstream licenses. Include the `third_party_sources/` directory and license files when sharing the generated Windows bundle.
