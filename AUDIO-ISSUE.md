# Menu effects investigation

Status: fixed and confirmed by the user on 2026-11-03, including the default
Release build with diagnostic flags and capture disabled. Normal launch is
`tools/run-local.cmd`. Player deformation remains fixed independently; preserve
the regeneration-safe vector packing correction described in MODEL-FIX.md.

## Confirmed fix

`port/src/xma_fix/` provides a local XMA implementation with the complete new
Canary Work/Decode/loop/packet state machine, adapted to the installed SDK ABI.
XMAEnableContext is routed to it, and kicks wait for its dedicated worker.
This is the default without environment variables. `FIFA07_STOCK_XMA=1` is an
explicit regression-control escape hatch. The original runtime DLL is unchanged.

The user confirmed `xma-full-reference-1` fixed the effects, then confirmed
`xma-fix-default-1` also sounded correct. Release compilation passed. Evidence:
`logs/audio-fix-verification.json`, both trial logs, and their reports.

This establishes that complete XMA state-machine parity fixes the reported
effects. It does not isolate one particular loop or buffer change as the sole
cause. Earlier partial backports failed; host output was already excluded by
the distorted guest PCM recording. Full-match audio and missing cutscenes are
not established as fixed by this menu check.

To reproduce local source adaptation, run `tools/prepare-xma-fix.py` against
the pinned SDK source and saved reference in `tools/xma_reference/`. Normal
builds compile the checked-in adapted sources directly. No SDK DLL replacement
or game-asset modification is required.

## User observations

- Only menu click/transition effects after Start sound distorted; music is normal.
- Xenia Canary f448664 with its default `new` XMA decoder sounds normal.
- The same reference with `--xma_decoder=master` sounds distorted like the port.
- Inline/worker scheduling, raw frame output, partial packet backports and the
  corrected XMAEnableContext routing did not audibly fix the effects.
- The user listened to `logs/menu-effects-before-playback.wav` and confirmed it
  has the same distortion. This is derived from the game's submitted PCM before
  SDL, so the fault is already present before host playback.

## Evidence and experiments

`port/src/audio_diagnostics.cpp` records the guest's six planar 256-sample BE
float channels at 48 kHz when FIFA07_TRACE_AUDIO=1. The baseline contains no
nonfinite or out-of-range samples; this does not establish that decoding is
correct. Preserve `logs/menu-audio-baseline-6ch.f32be`.

The installed SDK's XMA context already contains partial new-decoder backports,
plus 192-sample carry realignment. It uses an older FFmpeg fork than Canary.
Removing carry alone did not fix the reported sound.

The original diagnostic factory replaced the XMA decoder only with
FIFA07_TEST_XMA=1. A strong XMAEnableContext shim was necessary:
the stock kernel otherwise executes its own inline Work call inside the DLL,
bypassing most of the experiment. Ordinary launches forward to the stock entry.
The final factory now enables the verified path by default. The original
runtime DLL and game assets are unchanged.

On 2026-11-03, the preparation script was extended to transplant the
complete Work/Decode/loop/packet state machine from the saved Canary source
`logs/xenia-xma-new.cc`. This retains the installed class and FFmpeg ABI. It is a
control experiment that the user confirmed fixed the effects. Trial:
`xma-full-reference-1`. The cleaned reproducible version is
`tools/prepare-xma-fix.py`.

The diagnostic trial used FIFA07_TRACE_XMA=1 to capture decoded frames in
`logs/xma-reference-packets.bin`: eight little-endian uint32 header words
(magic 0x31414D58, context ID, sequence, packet bytes, sample rate, channels,
samples per channel, reserved), followed by encoded packet bytes and planar LE
float32 decoded samples. Capture is bounded at 6000 frames.
`tools/inspect-xma-capture.py` creates listening copies and per-context metrics.

The final default build removes that packet recorder and per-frame trace logs.
The codec implementation remains the older SDK version; upgrading FFmpeg was
not needed to fix the reported menu effects.
