"""Preserve float pack source bits before aliased destination writes.

Postprocess output from the pinned official nightly without rebuilding its SDK.
Only FLOAT16_2/4 instructions with identical source and destination are changed.
"""
from pathlib import Path
import argparse
import re

p=argparse.ArgumentParser()
p.add_argument('directory',type=Path)
a=p.parse_args()
changed=0
for path in a.directory.glob('*.cpp'):
    original=path.read_text(encoding='utf-8-sig')
    pattern=r'(\t// vpkd3d128 v(\d+),v\2,(3|5),[^\n]*\n)(.*?)(?=\n\s*// |\nloc_|\n\})'
    def fix(match):
        global changed
        comment,reg,mode,body=match.groups()
        if 'const PPCVRegister pack_source' in body:return match[0]
        # The emitted destination is u16; all u32/f32 accesses to this same
        # register are source reads. In particular the final sign read must
        # precede the destination's magnitude write.
        source=f'ctx.v{reg}'
        if source+'.u16[' not in body:raise RuntimeError(f'Unexpected pack layout in {path}')
        rewritten=body.replace(source+'.u32[','pack_source.u32[').replace(source+'.f32[','pack_source.f32[')
        changed+=1
        return comment+'\t{\n\tconst PPCVRegister pack_source = '+source+';\n'+rewritten+'\n\t}'
    fixed=re.sub(pattern,fix,original,flags=re.S)
    if fixed!=original:path.write_text(fixed,encoding='utf8')
print(f'Preserved source bits for {changed} aliased float pack instructions')
