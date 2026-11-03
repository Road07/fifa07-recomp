# FIFA 07 player deformation fix — 2026-11-02

The user confirmed that the curved arms/legs and apparently incomplete body are
fixed both in the player menu and on the pitch (`vpk-alias-visual-1`). Audio
distortion and missing opening/pre-match sequences remain separate open issues.

## Cause

The pinned ReXGlue nightly emits an unsafe `vpkd3d128` float-to-half conversion
when its source and destination are the same vector register. For example,
`vpkd3d128 v3,v3,5,2,2` writes the packed magnitude into the high half of the
source word before reading that word's original sign. A negative first component
therefore becomes positive. The original instruction reads its input before
writing its output.

FIFA's animation preparation uses 64 such aliased float pack instructions in
four generated translation units. The resulting packed animation values lost
signs before the pose decoder received them. The later matrix multiplications
and GPU palette uploads could be internally consistent while drawing the wrong
pose. This explains why earlier GPU/readback and serial-animation changes did
not help.

## Change and regeneration

`tools/fix-vpkd3d-alias.py` preserves a complete source vector before the emitted
FLOAT16_2/4 pack writes. It changes only instructions with identical source and
destination registers and is idempotent. The official SDK binaries are unchanged.

The normal build applies the correction to existing generated output. The
project CMake code-generation command also applies it after regeneration.
No diagnostic environment flag is required to use the fix. Launch normally with
`tools/run-local.cmd`.

The instruction recorder is retained for reproducibility but excluded from normal
builds; enable `FIFA07_BUILD_ANIMATION_AUDIT` explicitly to compile it.

## Verification

* `tools/vmx_harness/instr_vpkd3d_alias.s` contains the two aliased instructions
  with negative inputs and a distinct-register control case.
* Original nightly output: 472 vector tests, two failures, both aliased cases;
  the distinct-register control passes (`logs/vpk-alias-before.log`).
* Corrected output: all 472 cases pass (`logs/vpk-alias-after.log`). Reproduce
  with `tools/test-vmx.ps1 -FixPackAlias`.
* Before the fix, shoulder joints 8 and 112 were 0.646 cm apart; after the fix,
  38.255 cm. Different poses prevent direct position comparisons, but the
  collapsed shoulders are gone. Arm bone lengths stay approximately 27.1 cm.
  See `logs/skeleton-comparison.json` and `tools/compare-skeleton-captures.py`.
* The user confirmed normal player models in the menu and on the pitch.

The earlier bounded cube sharing fix for loading-time physical OOM remains in
place. These checks do not establish complete match, save, audio or video support.
