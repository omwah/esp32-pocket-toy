#!/usr/bin/env python3
"""Validate the complete production-style migration and package references."""
import json, re, struct, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
EYES=ROOT/'data'/'eyes'
manifest=json.loads((ROOT/'validation'/'migrated-styles.json').read_text())
errors=[]
styles=manifest.get('styles',[])
if len(styles)!=manifest.get('productionStyleCount'): errors.append('style count does not match manifest')
ids=[x.get('id') for x in styles]
if len(ids)!=len(set(ids)): errors.append('duplicate package ID in manifest')

def load_config(path):
    text=path.read_text()
    text=re.sub(r'//.*','',text)
    return json.loads(text)

def check_bmp(path,bits):
    b=path.read_bytes()
    if len(b)<54 or b[:2]!=b'BM': return 'invalid BMP'
    off=struct.unpack_from('<I',b,10)[0]; w,h=struct.unpack_from('<ii',b,18); bpp=struct.unpack_from('<H',b,28)[0]; comp=struct.unpack_from('<I',b,30)[0]
    row=((abs(w)*bpp+31)//32)*4
    if w<=0 or not h or bpp!=bits or comp or off+row*abs(h)>len(b): return 'unsupported or truncated BMP'

def check_wav(path):
    b=path.read_bytes()
    if len(b)<12 or b[:4]!=b'RIFF' or b[8:12]!=b'WAVE': return 'invalid WAV'

for item in styles:
    pid=item.get('id',''); package=EYES/pid; config=package/'config.eye'
    if item.get('status')!='pass': errors.append(f'{pid}: not accepted')
    if not config.is_file(): errors.append(f'{pid}: missing config.eye'); continue
    try: cfg=load_config(config)
    except Exception as exc: errors.append(f'{pid}: config parse: {exc}'); continue
    for key in ('irisTexture','scleraTexture'):
        if key in cfg:
            p=package/Path(cfg[key]).name
            if not p.is_file(): errors.append(f'{pid}: missing {key}')
            else:
                err=check_bmp(p,24)
                if err: errors.append(f'{pid}: {p.name}: {err}')
    for key in ('upperEyelid','lowerEyelid'):
        if key in cfg:
            p=package/Path(cfg[key]).name
            if not p.is_file(): errors.append(f'{pid}: missing {key}')
            else:
                err=check_bmp(p,1)
                if err: errors.append(f'{pid}: {p.name}: {err}')
    for name in cfg.get('extensions',{}).get('audio',{}).get('sounds',[]):
        p=package/Path(name).name
        if not p.is_file(): errors.append(f'{pid}: missing sound {name}')
        else:
            err=check_wav(p)
            if err: errors.append(f'{pid}: {p.name}: {err}')
if errors:
    print('\n'.join(errors),file=sys.stderr); sys.exit(1)
print(f'Validated {len(styles)} production styles and their package assets.')
