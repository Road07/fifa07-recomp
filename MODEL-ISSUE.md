# Player geometry investigation — 2026-10-30

Status: unresolved. The user clarified that the body is NOT actually missing a
left/right half: arms and legs bend like fish hooks, making the body appear
incomplete. This affects close-ups and players on the pitch. The earlier
missing-surface interpretation is superseded. No geometry fix is validated.

Additional user-reported symptoms: distorted/crackling audio after Start at the
first menu, and missing sequences both among opening EA/FIFA videos and among
pre-match player/stadium scenes. A common cause is possible but not established.

User supplied a screenshot, preserved as `logs/player-deformation-user.png`.
It shows Ronaldinho in the arena menu with a conspicuously curled hand/wrist
outline and an apparently compressed/overlapping leg silhouette. This single
still does not establish missing triangles or prove a specific skinning fault;
the reported persistent fish-hook curvature remains the symptom to reproduce.
Do not describe this as a confirmed missing half-body.

The opt-in serial animation trial (`logs/serial-animation-1.json`) verified
`DISTRIBUTE_ANIM original=1 effective=0` in the log. The user reported the same
distortion. This option is not enabled by the normal launcher and is not a fix.

## Completed comparisons

All comparisons used Release EXE SHA256
`DE02297E956ED25197FC9027B1D93A60D8A739BF457170476A30F3D21E20FEEB`
and the official nightly runtime. Cube sharing remained enabled.

| Trial | Change | User-observed result |
| --- | --- | --- |
| `logs/limbs-synchronous-1.json` | Wait for current memexport readback | Same distortion |
| `logs/limbs-preserve-pages-1.json` | Disable per-frame GPU page-state refresh | Same distortion |
| `logs/limbs-no-memexport-readback.json` | Disable memexport readback | Same distortion |

These flags did not fix the observed problem. They do not prove that every
GPU synchronization or memory-coherence path is correct. No experimental flag
was added to the normal launcher.

The independent instruction harness passed 469 vector cases, including vector
loads/stores and high-register/alias variants (`logs/vmx-extended-4.log`). This
covers individual instructions, not the complete animation or rendering path.

## Concrete code path identified

* `sub_82588620` constructs `TextureSkinning`, allocating three 256x128
  four-component 32-bit-float textures via `sub_82689C28`.
* `sub_82586E08` registers a model and stores its palette coordinates.
* `sub_82585A20` rotates the three textures, locks the selected texture with
  `sub_822519F0`, fills matrices using the returned data pointer and row pitch,
  then unlocks using `sub_82250EC8`.
* `sub_82584138` multiplies two matrices and writes a transposed 3x4 result
  (48 bytes) for each bone. The loop advances input matrices by 64 bytes.
* Global `0x82D40DD8` holds the TextureSkinning object; `0x82D900D8` holds the
  current texture. Static vtable at `0x82093D10` includes update method
  `sub_82585A20`.

Malformed bone transforms or incorrect GPU sampling of the palette could
explain both stretched limbs and triangles moving outside the visible model.
This remains a hypothesis. Index-buffer interpretation, missing mesh draws,
and culling remain possible; the user's description alone cannot rank these
reliably.

Next discriminating evidence: capture one affected draw and compare CPU palette
values, lock pointer/pitch, uploaded GPU texture and shader bone indices; inspect
post-transform vertices and culling. Do not present another generic readback
toggle as a confirmed fix. Keep the verified loading-time OOM fix separate.

## Runtime observations from opt-in diagnostics

`port/src/skinning_diagnostics.cpp` is inactive unless
`FIFA07_TRACE_SKINNING=1`. It calls the original guest functions and observes
their results. The normal launcher does not enable it.

* `logs/skinning-trace-1.log`: populated frames contained 502 bone products;
  independent scalar affine-product/transpose checks reported zero mismatch,
  zero nonfinite results, and zero writes outside the locked 256x128 palette.
  This validates this multiplication stage, not upstream poses or animation.
* `logs/skinning-bind-1.log`: palette binds use texture slot 16. CPU physical
  addresses and final guest D3D fetch addresses match, including the E/F alias
  4 KiB adjustment. Do not apply another 4 KiB correction.
* Shader microcode and translated DXBC were saved under
  `logs/skinning-bind-1-shaders`. The translated skinning vertex shader reads
  vertex fetch 95; the microcode disassembler labels it vf0 using its reversed
  display convention (95 minus storage index). This is not a translator rewrite. Therefore
  the first diagnostic sampler targeting vf0 did not capture body vertices;
  its empty vertex files are NOT evidence of missing game geometry.
* GPU register/palette samples under `logs/skinning-gpu-*.bin` are asynchronous
  CPU observations of register state. They do not capture host GPU texture
  contents or an exact draw. Shader constants may belong to different draws.
* Local ALU and 2D fetch translation logic was compared against official Xenia
  master. No obvious relevant semantic difference was found; this does not
  validate runtime uploads or generated shader execution. Comparison inputs
  are saved under `logs/upstream-shader-compare`.
* `logs/big-archive-index.json` indexes 15,087 entries in the seven BIG archives.
  Audio ABK banks such as initialization and UI events are present. Several
  logged missing AST probes have ABK counterparts; a failed loose-file probe
  alone is not proof of lost game assets. Opening WMV and VP6 files also exist
  under `game/data/movies`. Their presence does not prove playback works.

## 2026-11-02: resolved

The player deformation was fixed by preserving source bits in aliased
`vpkd3d128` float packing. The user confirmed normal models in both the menu
and on the pitch. See `MODEL-FIX.md` for the final cause, change and checks.
The observations below describe the diagnosis before that correction.

## 2026-11-02: reference and active CPU pose comparison

The user confirmed normal limbs with official Xenia Canary f448664 using the
same extracted default.xex and game assets. The port remains visibly distorted.
Reference captures are in `logs/xenia-skinning-reference-fixed`; the earlier
directory without `-fixed` has a bad alias interpretation and must not be used.
Inverse bind matrices for actors 23, 24 and 25 are byte-identical in both runtimes.

`logs/port-active-resume-1` captures an active CPU pose. Actor 24 has right/left
shoulder joints 8 and 112 only 0.646 cm apart; the reference has 35.771 cm. Port
clavicle origins 7 and 111 coincide exactly. Upper/lower arm lengths remain
about 27.1 cm, close to the reference's 27.2 cm. These are different animation
frames, so absolute positions and rotations cannot be compared directly.
The synchronous matrix hook independently recorded the same collapsed port
shoulder positions, ruling out asynchronous sampling for that observation.
Its mutable `logs/port-bone-products.bin` is overwritten by later diagnostic
runs and now contains the corrected pose; use the dated capture directories
and report for before/after comparisons.

Reproduce the invariant report with `tools/compare-skeleton-captures.py`;
results are saved in `logs/skeleton-comparison.json`. This is evidence of a
CPU pose problem upstream of palette multiplication, not a completed fix.
An opt-in pose-page write trap (`FIFA07_TRACE_POSE_WRITER=1`, together
with `FIFA07_TRACE_SKINNING=1`) identifies the native writer and current guest
LR. It only arms on an ordinary writable page and restores its original
protection before resuming the first write. Normal launches do not enable it.

`logs/pose-writer-3.log` identified the copy caller `sub_8270B268`, the packed
animation decoder, and hierarchy helper `sub_82703748`. Independent scalar
checks of the latter passed. The decoder audit captured 13,944 vector operations;
all checked ordinary values and instruction-register encodings agreed with
scalar semantics. This narrowed the problem to data prepared before decoding.
The failed alternate-decoder trial (`alternate-pose-1`) made players disappear
and is not enabled normally. It is not a fix.
