"""Check observed decoder vector operations using scalar guest-order semantics."""
from pathlib import Path
import collections
import json
import math
import struct

root=Path(__file__).resolve().parent.parent
instructions=json.loads((root/'logs/animation-vector-instructions.json').read_text())
data=(root/'logs/animation-vector-audit.bin').read_bytes()
assert len(data)%128==0
u32=lambda b:list(struct.unpack('>4I',b))
f32=lambda b:list(struct.unpack('>4f',b))
packi=lambda v:struct.pack('>4I',*(x&0xffffffff for x in v))
packf=lambda v:struct.pack('>4f',*v)
def rounded(x):
    try:return struct.unpack('>f',struct.pack('>f',x))[0]
    except OverflowError:return math.copysign(math.inf,x)
counts=collections.Counter(); failures=[]; skipped=collections.Counter()
for number,offset in enumerate(range(0,len(data),128)):
    record=data[offset:offset+128]
    ident,count,address=struct.unpack_from('<3I',record)
    ins=instructions[ident];op=ins['opcode'];args=ins['args'].split(',')
    inputs=[record[12+i*16:28+i*16][::-1] for i in range(count)]
    actual=record[76:92][::-1];memory=record[92:108]
    expected=None;floating=False
    if op=='lvx128':expected=memory
    elif op=='lvlx':expected=memory[address%16:]+bytes(address%16)
    elif op=='lvrx':expected=bytes(16-address%16)+memory[:address%16]
    elif op in ('vor','vor128','vsel','vsel128'):
        if op.startswith('vor'):expected=bytes(x|y for x,y in zip(inputs[1],inputs[2]))
        else:expected=bytes((x&~m)|(y&m) for x,y,m in zip(inputs[1],inputs[2],inputs[3]))
    elif op in ('vspltw','vspltw128'):
        i=int(args[-1]);expected=inputs[1][i*4:i*4+4]*4
    elif op.startswith('vspltisw'):
        i=int(args[-1]);i=(i&31)-(32 if i&16 else 0);expected=packi([i]*4)
    elif op=='vpermwi128':
        imm=int(args[-1]);v=u32(inputs[1]);expected=packi([v[(imm>>(6-i*2))&3] for i in range(4)])
    elif op=='vrlimi128':
        mask,rotation=map(int,args[-2:]);dst=u32(inputs[0]);src=u32(inputs[1])
        expected=packi([src[(i+rotation)%4] if mask&(8>>i) else dst[i] for i in range(4)])
    elif op=='vsldoi':
        shift=int(args[-1]);expected=(inputs[1]+inputs[2])[shift:shift+16]
    elif op=='vmrglw':
        a,b=u32(inputs[1]),u32(inputs[2]);expected=packi([a[2],b[2],a[3],b[3]])
    elif op=='vupkd3d128':
        mode=int(args[-1]);src=inputs[1]
        if mode==20:
            halves=struct.unpack('>4H',src[8:]);values=struct.unpack('>4e',src[8:])
            if any((x&0x7c00)==0 and x&0x3ff for x in halves):
                skipped['half subnormal conversion']+=1;continue
            expected=packf(values)
        elif mode==4:
            shorts=struct.unpack('>2h',src[12:]);expected=packi([0x40400000+shorts[0],0x40400000+shorts[1],0,0x3f800000])
        else:skipped[op+':'+str(mode)]+=1;continue
    elif op in ('vaddfp','vaddfp128','vsubfp','vsubfp128','vmulfp128','vmaddfp','vmaddcfp128','vmsum4fp128','vrsqrtefp'):
        floating=True
        a=f32(inputs[1]);b=f32(inputs[2]) if count>2 else None
        if op.startswith('vadd'):values=[rounded(x+y) for x,y in zip(a,b)]
        elif op.startswith('vsub'):values=[rounded(x-y) for x,y in zip(a,b)]
        elif op.startswith('vmul'):values=[rounded(x*y) for x,y in zip(a,b)]
        elif op.startswith('vmadd'):values=[rounded(rounded(x*y)+z) for x,y,z in zip(a,b,f32(inputs[3]))]
        elif op=='vmsum4fp128':values=[sum(rounded(x*y) for x,y in zip(a,b))]*4
        else:values=[1/math.sqrt(x) if x>0 else math.inf if x==0 else math.nan for x in a]
        expected=packf(values)
    elif op=='vcmpgtfp':expected=packi([0xffffffff if x>y else 0 for x,y in zip(f32(inputs[1]),f32(inputs[2]))])
    else:skipped[op]+=1;continue
    counts[op]+=1
    if floating:
        equal=all((math.isnan(x) and math.isnan(y)) or x==y or abs(x-y)<=3e-6*(1+abs(y)) for x,y in zip(f32(actual),f32(expected)))
    else:equal=actual==expected
    if not equal and len(failures)<100:
        failures.append(dict(record=number,instruction=ins,inputs=[v.hex() for v in inputs],actual=actual.hex(),expected=expected.hex()))
report=dict(records=len(data)//128,checked=dict(counts),skipped=dict(skipped),failures=failures)
(root/'logs/animation-vector-audit-report.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
