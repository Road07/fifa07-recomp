"""Extend known-answer VMX128 cases by register renaming and binary-op aliasing."""
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parent.parent
cases = []
binary = {'vaddfp128','vand128','vandc128','vmaxfp128','vminfp128',
          'vmsum3fp128','vmsum4fp128','vor128','vsubfp128','vxor128'}
for path in (root/'tools/rexglue-source/tests/ppc/asm').glob('*128.s'):
    text = path.read_text(encoding='utf-8-sig')
    starts = list(re.finditer(r'^test_[^\s:]+:', text, re.M))
    if not starts:
        continue
    symbols = {parts[2]:int(parts[0],16) for line in (root/'logs/vmx/bin'/f'{path.stem}.map').read_text().splitlines()
               if len(parts:=line.split())==3}
    machine = (root/'logs/vmx/bin'/f'{path.stem}.bin').read_bytes()
    for index, start in enumerate(starts):
        block = text[start.start():starts[index+1].start() if index+1<len(starts) else len(text)]
        instructions = [line.strip().split('#',1)[0].strip() for line in block.splitlines()[1:]
                        if line.strip() and not line.lstrip().startswith('#')]
        if len(instructions) != 2 or instructions[-1] != 'blr':
            continue
        match = re.fullmatch(r'(\w+128)\s+v(\d+),\s*v(\d+)(.*)', instructions[0])
        if not match or any(int(v)>=32 for v in re.findall(r'\bv(\d+)\b', block)):
            continue
        variants = [block]
        original_name = re.match(r'([^:]+):',block).group(1)
        original_word = struct.unpack_from('>I',machine,symbols[original_name])[0]
        opcode, dest, first, rest = match.groups()
        second = re.fullmatch(r',\s*v(\d+)',rest)
        if opcode in binary and second:
            for target in sorted({first, second.group(1)} - {dest}):
                changed = block.replace(instructions[0], re.sub(r'\bv\d+', 'v'+target, instructions[0], count=1), 1)
                lines = []
                for line in changed.splitlines():
                    output = re.search(r'#_ REGISTER_OUT v(\d+)\b', line)
                    if output and output.group(1) == target:
                        continue
                    if output and output.group(1) == dest:
                        line = re.sub(r'REGISTER_OUT v'+dest+r'\b', 'REGISTER_OUT v'+target, line)
                    lines.append(line)
                changed = '\n'.join(lines)+'\n'
                changed = re.sub(r'^(test_[^:]+):',r'\1_alias'+target+':',changed,count=1)
                variants.append(changed)
        for variant in variants:
            for offset in (32,64,96):
                renamed = re.sub(r'\bv(\d+)\b',lambda m:'v'+str(int(m.group(1))+offset),variant)
                renamed = re.sub(r'^(test_[^:]+):',r'\1_hi'+str(offset)+':',renamed,count=1)
                # High-register encoding follows Xenia's independent InstrData
                # VX128 / VX128_3 / VX128_4 field definitions:
                # https://github.com/xenia-project/xenia/blob/master/src/xenia/cpu/ppc/ppc_instr.h
                lines=[]
                for line in renamed.splitlines():
                    if line.strip() and not line.lstrip().startswith('#') and '128 ' in line:
                        regs=list(map(int,re.findall(r'\bv(\d+)\b',line)))
                        d,b=regs[0],regs[-1]
                        word=original_word & ~((31<<21)|(3<<2)|(31<<11)|3)
                        word|=(d&31)<<21 | (d>>5)<<2 | (b&31)<<11 | b>>5
                        if len(regs)==3:
                            a=regs[1]
                            word&=~((31<<16)|(1<<5)|(1<<10))
                            word|=(a&31)<<16 | ((a>>5)&1)<<5 | ((a>>6)&1)<<10
                        line=f'  .long 0x{word:08X} # {line.strip()}'
                    lines.append(line)
                renamed='\n'.join(lines)+'\n'
                cases.append(renamed)
output=root/'logs/vmx/asm/instr_vmx_high_registers.s'
output.write_text('\n'.join(cases),encoding='ascii')
print(f'Generated {len(cases)} high-register / aliased-source known-answer cases')
