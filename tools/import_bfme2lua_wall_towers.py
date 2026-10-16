#!/usr/bin/env python3
"""Build a local, untracked tower asset pack from BFME2Lua and extracted retail INIs."""
import argparse, re, struct
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--retail-ini', type=Path, required=True)
p.add_argument('--output', type=Path, default=Path('godot/mods/wall_towers.big'))
a=p.parse_args()
files={}; patches={}
for archive in sorted((a.source/'assets/rotwk').glob('*wall-tower.big')):
    data=archive.read_bytes(); at=16
    for _ in range(struct.unpack_from('>I',data,8)[0]):
        off,size=struct.unpack_from('>II',data,at); at+=8
        end=data.index(0,at); name=data[at:end].decode(); at=end+1
        payload=data[off:off+size]
        if name.endswith(('commandset.ini','commandbutton.ini','upgrade.ini')):
            text=payload.decode().replace('\r','')
            blocks=re.findall(r'(?ms)^(?:CommandSet|CommandButton|Upgrade)\s+[^\n]+\n.*?^End[^\n]*',text)
            for block in blocks:
                if 'BFME2Lua' in block: patches.setdefault(name,{})[block.splitlines()[0]]=block
        elif name.endswith(('elvencastlewalls.ini','isengardcastlewalls.ini')):
            original=(a.retail_ini/Path(name.replace('\\','/')).relative_to('data/ini')).read_text().replace('\r','')
            modified=payload.decode().replace('\r','')
            roots=re.findall(r'(?ms)^(?:Object|ChildObject)\s+[^\n]+\n.*?^End[^\n]*',modified)
            for root in roots:
                if 'BFME2Lua' not in root: continue
                object_name=root.splitlines()[0].split()[1]
                pattern=r'(?ms)^(?:Object|ChildObject)\s+'+re.escape(object_name)+r'(?:\s+[^\n]*)?\n.*?^End[^\n]*'
                native=re.search(pattern,original)
                if not native: raise SystemExit('Missing native object '+object_name)
                updated=native.group()
                for module in re.findall(r'(?ms)^[ \t]+Behavior\s*=\s*ReplaceSelfUpgrade[^\n]*\n.*?^[ \t]+End[^\n]*',root):
                    if 'BFME2Lua' not in module: continue
                    tag=module.splitlines()[0].split()[-1]
                    mod_pattern=r'(?ms)^[ \t]+Behavior\s*=\s*ReplaceSelfUpgrade\s+'+re.escape(tag)+r'[^\n]*\n.*?^[ \t]+End[^\n]*'
                    if re.search(mod_pattern,updated): updated=re.sub(mod_pattern,lambda _:module,updated,count=1)
                    else: updated=updated[:updated.rfind('End')]+module+'\nEnd'
                original=original[:native.start()]+updated+original[native.end():]
            files[name]=original.encode()
        else: files[name]=payload
if not patches: raise SystemExit('No BFME2Lua tower archives found')
for name,blocks in patches.items():
    text=(a.retail_ini/name.split('\\')[-1]).read_text().replace('\r','')
    for header,block in blocks.items():
        pattern=r'(?ms)^'+re.escape(header)+r'[^\n]*\n.*?^End[^\n]*'
        if re.search(pattern,text): text=re.sub(pattern,lambda _:block,text,count=1)
        else: text+='\n'+block+'\n'
    files[name]=text.encode()
size=16+sum(8+len(name.encode())+1 for name in files)
index=bytearray(); content=bytearray()
for name,data in sorted(files.items()):
    index+=struct.pack('>II',size+len(content),len(data))+name.encode()+b'\0'; content+=data
result=b'BIGF'+struct.pack('>III',size+len(content),len(files),size)+index+content
a.output.parent.mkdir(parents=True,exist_ok=True); a.output.write_bytes(result)
print(f'{a.output}: {len(files)} entries, {len(result)} bytes')
