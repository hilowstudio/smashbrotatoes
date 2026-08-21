# Building SmashBrotatoes

SmashBrotatoes builds with CMake. `libultraship` and `torch` are vendored
directly in this repository (no submodules to initialize). This is a US-only
fork (NTSC-U v1.0, `NALE`).

The flow is **three commands**: configure, build, run. Asset extraction from
your ROM happens automatically as part of the build (cached afterwards) —
there is no separate extract step.

SmashBrotatoes is Windows-only. Builds use CMake + Visual Studio 2022 (MSVC)
with the DirectX11/OpenGL renderer and WASAPI audio.

## Prerequisites

- Windows
- Visual Studio 2022 + Windows SDK (MSVC toolchain)
- CMake
- Python 3 with Pillow (`pip install Pillow`)
- Git
- A legal `baserom.us.z64` at the repo root:

  | Version | Game code | SHA‑1 | MD5 |
  |---------|-----------|-------|-----|
  | US (NTSC-U v1.0) | `NALE` | `e2929e10fccc0aa84e5776227e798abc07cedabf` | `f7c52568a31aadf26e14dc2b6416b2ed` |

  A dump that doesn't match these hashes will not work.

`.z64` is shown above; `.n64`/`.v64` are also accepted.

## Build

From a Developer PowerShell, replace `<ver>` with `us` or `jp`
(`cmake.exe` is typically at `C:\Program Files\CMake\bin\cmake.exe`):

```powershell
# 1. configure
cmake -S . -B "build\<ver>" -A x64 -DSSB64_VERSION=<ver>

# 2. build (compiles the game and extracts your ROM's assets)
cmake --build "build\<ver>" --config Release

# 3. run
.\build\<ver>\Release\SmashBrotatoes.exe
```

Use `--config Debug` (and run `.\build\<ver>\Debug\SmashBrotatoes.exe`) for a
debug build. CMake auto-detects the newest installed Visual Studio; pin
it with `-G "Visual Studio 17 2022" -T v143` if a runner has several.

## Notes

- Each version's assets and binary live entirely in that version's build
  directory; US and JP never clobber each other. Run the binary from its
  build dir (it loads `SmashBrotatoes.o2r` relative to the working
  directory).
- Switching versions is just a different build dir + `-DSSB64_VERSION`;
  both can be built and kept side by side.
- A normal build also produces the standalone `torch` sidecar and copies
  `config.yml`, `yamls/<ver>`, `f3d.o2r`, `gamecontrollerdb.txt`, and the
  menu fonts next to the executable.
- The checked-in reloc YAMLs (`yamls/<ver>/reloc_*.yml`) and the
  generated `port/resource/RelocFileTable.<ver>.cpp` are treated as
  source inputs, not rebuilt on every compile.

### Advanced / manual targets

Normally unnecessary (the build does these for you). Append the target to
`cmake --build build-<ver>`:

| Target | Purpose |
|--------|---------|
| `ExtractAssets` | Re-extract `SmashBrotatoes.o2r` from the ROM |
| `ExtractAssetHeaders` | Regenerate generated build-input headers |
| `RegenerateRelocYamls` | Regenerate the checked-in reloc YAMLs |

Packaged release builds ship no ROM: CMake installs a sidecar `torch` +
`config.yml`, and the game extracts assets from a user-supplied ROM on
first run instead of at build time.
