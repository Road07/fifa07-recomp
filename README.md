# FIFA 07 ReXGlue port

Experimental Windows x64 static recompilation of the Xbox 360 version of FIFA 07.
Built against ReXGlue **0.10.0.24-dev.gbd833a2**, pinned to SDK commit
`bd833a2ab69d305f99ddf57418fc38ffec8af93d`.

The repository includes all port source code, generated game C++/headers, the
recompilation manifest, fixes, diagnostic tools and regression tests. Supply
your own game data; the source package contains no game binaries or assets.

## Status

User-verified in the default Release build:

- Menu and pitch player limb deformation corrected.
- Distorted menu transition/click effects corrected with the local XMA path.
- Opening videos and pre-match scenes displayed with the D3D12 ROV path.
- Loading-time physical memory exhaustion corrected by bounded sharing of
  identical small DDS cube textures.

Full-match completion, career save/reload, broad performance testing and online
features remain unverified. Some Xbox Live/network and console UI imports are
SDK stubs. See [PORT-CHECK.md](PORT-CHECK.md) for the audit and scope.

## Requirements

- Windows x64; tested on an NVIDIA RTX 4070 Laptop GPU.
- Visual Studio **2022 C++ Build Tools**, including Windows SDK, CMake and Ninja.
- LLVM/Clang **20.x** (validated with 20.1.8).
- Python **3.10+** and Git for Windows on PATH.
- A free drive letter **V:** during build/run (used to avoid Unicode path issues).
- Your own supported Xbox 360 game extraction.

Other operating systems and CPU architectures are not validated by this port.
Python packages in requirements-dev.txt are optional for diagnostic scripts.

## Setup and build

Run from the repository root in PowerShell:

```powershell
# Download the checksum-verified SDK release and checkout its exact source/FFmpeg.
.\tools\setup-dependencies.ps1

# Extract your own disc to a new game/ directory.
.\tools\Extract-Xdvdfs.ps1 -IsoPath 'D:\path\to\FIFA07.iso' -OutputPath '.\game'
.\tools\verify-game.ps1

.\tools\build-release.cmd
.\tools\run-local.cmd
```

Supported disc: Title ID `454107EF`, Media ID `0D1C159F`, version `0.0.0.5`.
Required default.xex SHA-256:
`1d5b1f38b3e5238edf9e68d57a6e8070a71f4296ff203087048d688f02fefd3a`.
The build refuses a different executable revision. The extraction manifest
`tools/game-files.json` records all 57 expected files.

Clang may be installed on PATH or at `tools/llvm-20/bin/clang++.exe`. Optional
explicit locations:

```powershell
$env:FIFA07_LLVM_ROOT = 'C:\Program Files\LLVM'
$env:FIFA07_PYTHON = 'C:\Python312\python.exe'
.\tools\build-release.cmd
```

The executable is `port/out/build/win-release/fifa07.exe`. The launcher provides
its game/user/cache directories; use it instead of moving the EXE on its own.
Do not build while the launcher is using V:. Build scripts do not overwrite an
existing V: mapping. Setup does not silently change an existing SDK checkout.

If a connection repeatedly interrupts the SDK download, download the pinned
release ZIP from the URL in `tools/dependencies.lock.json`, then run
`./tools/setup-dependencies.ps1 -SdkArchive 'D:\Downloads\rexglue-win-amd64.zip'`.
The same SHA-256 check is applied to a manually supplied archive.

## Source and fixes

| Area | Source / explanation |
|---|---|
| Application defaults | port/src/fifa07_app.h, port/src/graphics_defaults.cpp |
| Physical memory | port/src/cube_texture_cache.cpp, MEMORY-FIX.md |
| Player geometry | tools/fix-vpkd3d-alias.py, MODEL-FIX.md |
| Menu audio | port/src/xma_fix/, AUDIO-ISSUE.md |
| Cinematics | CINEMATIC-ISSUE.md |
| Generated executable | port/generated/default/, port/fifa07_manifest.toml |
| Dependency versions | tools/dependencies.lock.json |

The float-packing correction is applied after code regeneration. The local XMA
sources are checked in; tools/prepare-xma-fix.py reproduces their adaptation
from the pinned SDK and the saved Xenia reference. The SDK runtime and GPU DLLs
are unchanged. ROV is selected by the app before GPU initialization; an explicit
`--render_target_path_d3d12=rtv` remains available for comparison.
`FIFA07_STOCK_XMA=1` selects the original SDK XMA implementation for regression
comparisons. Ordinary use needs neither override.

Generated game C++ is included, but rebuilding/regenerating still requires the
supported game extraction. Generated dependency/stamp files are excluded because
they are build-local. Original game data is loaded at runtime from game/.

## Validation

```powershell
.\tools\test-memory-policy.cmd
.\tools\test-vmx.ps1 -FixPackAlias
.\tools\test-startup.ps1 -Attempt local-check-1 -DurationSeconds 30
```

The recorded audit passes **995,328** memory-policy cases and **472/472** vector
cases. A startup timer only checks the observed process interval; it does not
prove that a complete match or save/reload works. Startup test attempt names are
unique and tests close their own process at timeout. The normal launcher has
no test timeout.

The pinned GUI runtime currently returns a CLI11 parse error for `--help`;
use this README for commands. Normal startup was checked separately from that
help-argument limitation in the clean source build.

Cinematic and animation recorders are disabled by default. Enable
`FIFA07_BUILD_CINEMATIC_DIAGNOSTICS` / `FIFA07_BUILD_ANIMATION_AUDIT` in CMake only
for development; the cinematic recorder additionally requires
`FIFA07_TRACE_CINEMATICS=1`. Raw logs and media captures stay outside the repository.
Development reports may reference local logs that are not shipped. Selected
small validation summaries are included under docs/validation/ in the export.

## License and publishing

Newly authored port glue, scripts, tests and documentation are MIT licensed.
The generated game code is not relicensed by this project's MIT grant.
ReXGlue, Xenia and FFmpeg retain their own notices and licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

For the prepared source ZIP/folder and Git upload commands, see
[GITHUB_UPLOAD.md](GITHUB_UPLOAD.md). No repository is created or pushed by the
packaging tool.
