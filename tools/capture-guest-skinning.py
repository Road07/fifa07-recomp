"""Read only the known FIFA skinning structures from an owned game process."""
import argparse, ctypes as c, json, pathlib, struct, time

p=argparse.ArgumentParser()
p.add_argument('pid',type=int)
p.add_argument('output',type=pathlib.Path)
p.add_argument('--seconds',type=int,default=100)
p.add_argument('--active',action='store_true')
p.add_argument('--extra',action='append',default=[],help='Known guest range address:length:basename to read')
a=p.parse_args()
k=c.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[c.c_uint,c.c_int,c.c_uint];k.OpenProcess.restype=c.c_void_p
k.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes=[c.c_void_p]
h=k.OpenProcess(0x410,False,a.pid)
if not h:raise OSError(c.get_last_error(),'OpenProcess')
def read(address,n):
    buf=c.create_string_buffer(n);got=c.c_size_t()
    if not k.ReadProcessMemory(h,address,buf,n,c.byref(got)) or got.value!=n:
        raise OSError(c.get_last_error(),f'ReadProcessMemory {address:X}+{n:X}')
    return buf.raw
u32=lambda b,offset=0:struct.unpack_from('>I',b,offset)[0]
try:
    expected=pathlib.Path('logs/guest-image.bin').read_bytes()[0x1C0000:0x1C0010]
    base=None
    for bit in range(32,47):
        try:
            if read((1<<bit)+0x821C0000,16)==expected:base=1<<bit;break
        except OSError:pass
    if base is None:raise RuntimeError('FIFA code identity did not match at a supported guest base')
    end=time.monotonic()+a.seconds
    while time.monotonic()<end:
        try:
            obj=u32(read(base+0x82D40DD8,4))
            # Verify the alias correction against the exact expected vtable.
            correction=None
            for shift in ([0x1000,0] if obj>=0xE0000000 else [0]):
                try:
                    if u32(read(base+obj+shift,4))==0x82093D10:correction=shift;break
                except OSError:pass
            if correction is None:raise ValueError('TextureSkinning is not constructed yet')
            e_correction=correction if obj>=0xE0000000 else 0x1000
            def guest(addr,n):return read(base+addr+(e_correction if addr>=0xE0000000 else 0),n)
            state=guest(obj,856);count=u32(state,124);entries_ptr=u32(state,116)
            texture_probe=u32(state,96)
            if u32(guest(texture_probe,56),32)&63!=38:
                e_correction=0
                if u32(guest(texture_probe,56),32)&63!=38:raise ValueError('Palette texture is not ready')
            if not 1<=count<=512:raise ValueError('No registered skinned models yet')
            entries=guest(entries_ptr,count*20)
            populated=sum(bool(u32(guest(u32(entries,i*20+4)+8,4))) for i in range(count))
            if populated<2:raise ValueError('Waiting for character bind matrices')
            if a.active:
                if count<25:raise ValueError('Character entry is not registered yet')
                actor=u32(entries,24*20);world=u32(guest(actor+96,4))
                if not world or u32(guest(world+60,4))!=0x3F800000:
                    raise ValueError('Waiting for active character pose')
            a.output.mkdir(parents=True,exist_ok=True)
            for extra in a.extra:
                address,length,name=extra.split(':')
                if pathlib.Path(name).name!=name:raise ValueError('Extra range requires a basename')
                (a.output/name).write_bytes(guest(int(address,0),int(length,0)))
            (a.output/'object.bin').write_bytes(state)
            (a.output/'entries.bin').write_bytes(entries)
            models=[]
            for i in range(count):
                resource,mesh,anim,x,y=struct.unpack_from('>5I',entries,i*20)
                header=guest(mesh,16);bones=u32(header);bind=u32(header,8)
                record=dict(index=i,resource=resource,mesh=mesh,anim=anim,x=x,y=y,bones=bones,bind=bind)
                if resource:
                    record['vtable']=u32(guest(resource,4))
                    record['getter']=u32(guest(record['vtable']+24,4))
                    (a.output/f'resource-{i}.bin').write_bytes(guest(resource,256))
                    # This exact title getter is lwz r3,96(r3); blr.
                    if record['getter']==0x82886E58 and 1<=bones<=512:
                        world=u32(guest(resource+96,4));record['world']=world
                        if world:(a.output/f'world-{i}.bin').write_bytes(guest(world,bones*64))
                if 1<=bones<=512 and bind:(a.output/f'bind-{i}.bin').write_bytes(guest(bind,bones*64))
                models.append(record)
            for i in range(3):
                texture=u32(state,96+i*4);header=guest(texture,56)
                (a.output/f'texture-{i}.bin').write_bytes(header)
                address=(u32(header,32)&0x1FFFF000)+(0x1000 if u32(header,32)>=0xE0000000 else 0)
                (a.output/f'palette-{i}.bin').write_bytes(read(base+0x100000000+address,524288))
            meta=dict(pid=a.pid,virtual_base=base,physical_alias_correction=e_correction,object=obj,count=count,models=models)
            (a.output/'capture.json').write_text(json.dumps(meta,indent=2),encoding='utf8')
            print(json.dumps(meta,indent=2));break
        except (OSError,ValueError):time.sleep(0.013)
    else:raise RuntimeError('No populated skinning state captured within the deadline')
finally:k.CloseHandle(h)
