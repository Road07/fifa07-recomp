# Source provenance and licenses

The root MIT license covers newly authored port glue, scripts, tests and
documentation to the extent owned by this project's contributors. It does not
replace the licenses or rights of upstream and game-derived material.

| Material | Provenance | License / notice |
|---|---|---|
| ReXGlue SDK | rexglue/rexglue-sdk, commit bd833a2ab69d305f99ddf57418fc38ffec8af93d | BSD-3-Clause; LICENSES/ReXGlue-BSD-3-Clause.txt |
| Local XMA context/decoder | Adapted from the pinned ReXGlue SDK | Original notices retained; port/src/xma_fix/LICENSE.ReXGlue |
| Complete XMA state machine | Xenia Canary f448664, src/xenia/apu/xma_context_new.cc | BSD; LICENSES/Xenia-BSD-3-Clause.txt and tools/xma_reference/LICENSE |
| FFmpeg | wmarti/FFmpeg, commit 0604b464c7cb4ebc94940cf1f324a3b26b87717c | LGPL-2.1-or-later for the default library; LICENSES/FFmpeg-LGPL-2.1.txt; see upstream per-file notices/configuration |
| Generated game C++ | ReXGlue output from the supported FIFA 07 Xbox 360 executable | Original game-derived material; not relicensed by the port's MIT license |
| CMake integration | ReXGlue-generated port/generated/rexglue.cmake | ReXGlue attribution as above |

The original source comments and license files are retained. Additional SDK
dependencies have their own notices in the pinned upstream repository.
tools/setup-dependencies.ps1 obtains the exact SDK source and its FFmpeg
submodule; tools/dependencies.lock.json records the revisions and SDK archive
checksum. To build the SDK itself, initialize its remaining submodules with
`git -C tools/rexglue-source submodule update --init --recursive` and follow its
upstream build documentation.

FIFA 07, EA SPORTS and associated names/assets belong to their respective
owners. Original game binaries, disc images, movies, textures, audio and save
data are not included in the source export. Users supply their own supported
game data. Source publication does not imply endorsement by the game or SDK
authors.
