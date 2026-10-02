from pathlib import Path
import argparse,hashlib,json,sys
sys.path.insert(0,str(Path(__file__).parent))
from _cursor_codec import transform_ani,transform_cur
args=argparse.ArgumentParser()
args.add_argument('--source',type=Path,help='Optional installed Ikaros directory for source hash verification')
args=args.parse_args()
root=Path(__file__).resolve().parents[1]
resources=root/'resources'
manifest=json.loads((resources/'manifest.json').read_text(encoding='utf-8'))
checks=[]
for entry in manifest:
    name=entry['file']
    raw=(resources/'original'/name).read_bytes()
    assert hashlib.sha256(raw).hexdigest()==entry['sha256'],name
    if args.source:
        source=args.source/name
        assert hashlib.sha256(source.read_bytes()).hexdigest()==entry['sha256'],name
    parser=transform_ani if name.endswith('.ani') else transform_cur
    _,original=parser(raw)
    original_frames=original['frames'] if isinstance(original,dict) else [original]
    for folder,alpha in [('fade50',128),('fade75',64),('fade90',26),('hidden',0)]:
        _,variant=parser((resources/folder/name).read_bytes())
        frames=variant['frames'] if isinstance(variant,dict) else [variant]
        if isinstance(original,dict):
            for field in ['anih','rate_jiffies','sequence']:
                assert original[field]==variant[field],(name,folder,field)
        assert len(original_frames)==len(frames),(name,folder)
        for old,new in zip(original_frames,frames):
            assert len(old)==len(new),(name,folder)
            for a,b in zip(old,new):
                for field in ['width','height','hotspot_x','hotspot_y']:
                    assert a[field]==b[field],(name,folder,field)
                assert b['alpha_values']==([0] if alpha==0 else [0,alpha]),(name,folder,b['alpha_values'])
                assert b['xor_invert_pixels']==0,(name,folder)
        checks.append({'file':name,'variant':folder,'frames':len(frames),'alpha_max':alpha})
result={'passed':True,'original_files_verified':len(manifest),'variants_verified':len(checks),'checks':checks}
(root/'state').mkdir(exist_ok=True)
(root/'state'/'resource-verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='checks'},ensure_ascii=False))
