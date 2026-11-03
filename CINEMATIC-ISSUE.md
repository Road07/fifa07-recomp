# Missing cinematics investigation (2026-11-03)

Fixed and user-confirmed in the default Release build, including opening videos
and pre-match scenes. User corrected an earlier answer: the opening and pre-match
sequences **do appear in Xenia**. The port shows several seconds of black where
the EA opening video should be, then reaches the FIFA title screen.

Preserve the verified player packing and XMA menu-audio fixes.

## Confirmed observations

- Extracted WMV/VP6 files are present in game/data/movies.
- The port successfully opens 01_eng_us.wmv and 00_eng_us.wmv; no file-open
  failure is recorded for them. It also opened 02_eng_us.wmv in trial 1.
- The WMV constructor sub_8299DE08 returns success (0), with an object whose
  vtable is 0x82127EB0. The game-facing open returns movie ID 0, which is valid.
- Trial 5: 01_eng_us.wmv advances to timestamp 3566 ms and returns 0x16660026
  (the game's end-of-stream comparison) on call 122. ASF duration is 3.599 s.
- Trial 5: 00_eng_us.wmv successfully advances through timestamp 3069 ms at
  call 120. Its ASF duration is 10.01 s. The window was closed at ~5.8 s into
  this video, so that trial does not establish its final end-of-stream state.
- Both WMV streams identify WMV3 and 1280x720 in their bitmap headers.
- A screenshot during trial 4 shows the league-logo sequence on screen. This
  does not establish that the animated EA video renders correctly.
- Foreground switched between FIFA and other applications during screenshots;
  do not treat screenshots of other apps as evidence of the game's pixels.

## Diagnostic implementation

port/src/cinematic_diagnostics.cpp requires both the CMake option
FIFA07_BUILD_CINEMATIC_DIAGNOSTICS=ON and FIFA07_TRACE_CINEMATICS=1.
The default build excludes this recorder entirely.
NtCreateFile/NtOpenFile/NtQueryDirectoryFile wrappers forward to the original
DLL entry points. Game hooks forward to __imp__sub_... bodies, preserving their
behavior. Trial logs are logs/cinematic-trace-1.log through -6.log.

The first two game hooks used C++ linkage by mistake in trials 1-3, so their
absence from those logs is not evidence that they weren't called. Correct C
linkage is active from trial 4 onward. File/kernel hook logs remain valid.

Identified path:

- sub_82586878 selects movie ID and resolves the localized WMV path.
- sub_825D4000 allocates a movie slot; sub_825D2700 initializes the WMV player.
- sub_8299DE08 creates the player; sub_8299E4D0 is the memory-stream wrapper.
- sub_825D2B38 pumps the movie; sub_8299D768 dispatches its playback method.
- Player vtable +80 -> sub_829AB9B0 -> +76 -> sub_829ADD40.
- sub_829AB138 returns the decoded sample selected for presentation. Trial 6
  adds bounded raw sample capture by inspecting its simple data/size getters.

## Isolated failure and ROV result

Trial 8 captures complete YUV420 video samples (8-byte BE millisecond timestamp,
then 1280x720 Y plane and two 640x360 chroma planes). They show the correct EA
Sports white-background logo for 01_eng_us.wmv and animated EA HD / EA Sports
images for 00_eng_us.wmv. See logs/cinematic-yuv-luma.png and
tools/inspect-cinematic-yuv.py. The first generic getter experiment (trial 7)
also captured audio and used an advanced cursor; do not use those earlier
cinematic-frame-*.bin files as evidence of decoded video content.

cinematic-render-1 confirms 1280x720 screen and video dimensions, aspect=1,
rect=(0,0,1280,720), vertices=(0,0),(1280,0),(0,720), valid texture/shader objects
and status=0 from the renderer. The YUV pixel shader reached the backend:
hash 9EF298A63A9C23DD, three texture fetches and YUV conversion constants.

cinematic-fresh-shaders-1 used a separate empty cache and synchronous shader
compilation. User: still black. Thus stale cache / asynchronous compilation
does not explain the observed failure.

cinematic-rov-1 changed render_target_path_d3d12=rov (same synchronous compile
control). User: videos appeared. The SDK defaults to RTV on this NVIDIA GPU.
This isolates the working workaround to the D3D12 render-target path; the exact
internal RTV defect is not identified. There is no need to replace WMV files or
their software decoder.

port/src/graphics_defaults.cpp selects ROV when no explicit D3D12 target
path is configured, before presentation/GPU initialization. It respects an
explicit RTV CLI/config override. It is enabled in the app itself, so normal
launch does not require extra flags or changes to the launcher.

## Final validation

- ROV experiment: user confirmed opening videos appeared, then confirmed
  pre-match scenes appeared. An initial freeze was reported in that experiment,
  which explicitly disabled asynchronous shader compilation.
- Default Release: build passed, diagnostic recorder is OFF in CMakeCache,
  no cinematic tracing environment variable, no render-path CLI override and
  no synchronous shader override. Startup log confirms the app selects ROV.
- User confirmed: scenes appear and there is no noticeable initial freeze.
  This is an observation, not an instrumented performance benchmark. The earlier
  stall may have involved first-time pipeline compilation; that exact cause
  was not isolated.
- Pitch gameplay was visually observed with normal player geometry in the ROV
  experiment. Prior player and XMA menu-audio fixes are preserved.
- The original XEX, movie assets, SDK runtime DLL and GPU plugin DLL were not
  modified. The fix chooses an existing backend path.

Evidence: logs/cinematic-fix-default-1.log,
logs/cinematic-fix-default-1-cinematic-options.json,
logs/build-cinematic-fix-default-1.log and
logs/cinematic-fix-verification.json. Normal launch: tools/run-local.cmd.
Full-match completion and save behavior remain unverified.
