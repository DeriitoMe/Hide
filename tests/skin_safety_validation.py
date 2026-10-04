"""Windows skin safety regression: current skin only, no preference writes or text injection.

Exit Hide first. A brief in-memory Arrow override checks the mismatch protection.
Finally, restore the latest Windows configuration and verify its original visual identity.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import winreg

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--cycles', type=int, default=100)
parser.add_argument('--output', default='evidence/skin-safety.json')
args = parser.parse_args()
assert args.cycles > 0
EXE = ROOT / 'Hide.exe'
ROLES = ['Arrow','Help','AppStarting','Wait','Crosshair','IBeam','NWPen','No','SizeNS','SizeWE','SizeNWSE','SizeNESW','SizeAll','UpArrow','Hand','Pin','Person']
IDS = [32512,32651,32650,32514,32515,32513,32631,32648,32645,32644,32642,32643,32646,32516,32649,32671,32672]
user = c.WinDLL('user32', use_last_error=True)
gdi = c.WinDLL('gdi32', use_last_error=True)
def bind(lib, name, arguments, result):
    fn = getattr(lib, name); fn.argtypes = arguments; fn.restype = result; return fn
load = bind(user, 'LoadCursorW', [w.HINSTANCE,c.c_void_p], w.HANDLE)
load_file = bind(user, 'LoadImageW', [w.HINSTANCE,w.LPCWSTR,w.UINT,c.c_int,c.c_int,w.UINT], w.HANDLE)
draw = bind(user, 'DrawIconEx', [w.HDC,c.c_int,c.c_int,w.HANDLE,c.c_int,c.c_int,w.UINT,w.HBRUSH,w.UINT], w.BOOL)
destroy = bind(user, 'DestroyCursor', [w.HANDLE], w.BOOL)
set_cursor = bind(user, 'SetSystemCursor', [w.HANDLE,w.DWORD], w.BOOL)
find = bind(user, 'FindWindowW', [w.LPCWSTR,w.LPCWSTR], w.HWND)
send_message = bind(user, 'SendMessageTimeoutW', [w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)], c.c_ssize_t)
create_dc = bind(gdi, 'CreateCompatibleDC', [w.HDC], w.HDC)
create_dib = bind(gdi, 'CreateDIBSection', [w.HDC,c.c_void_p,w.UINT,c.POINTER(c.c_void_p),w.HANDLE,w.DWORD], w.HBITMAP)
select = bind(gdi, 'SelectObject', [w.HDC,w.HANDLE], w.HANDLE)
delete = bind(gdi, 'DeleteObject', [w.HANDLE], w.BOOL)
delete_dc = bind(gdi, 'DeleteDC', [w.HDC], w.BOOL)
flush = bind(gdi, 'GdiFlush', [], w.BOOL)
class Header(c.Structure):
    _fields_ = [('size',w.DWORD),('width',w.LONG),('height',w.LONG),('planes',w.WORD),('bpp',w.WORD),('compression',w.DWORD),('image_size',w.DWORD),('x',w.LONG),('y',w.LONG),('used',w.DWORD),('important',w.DWORD)]
class IconInfo(c.Structure):
    _fields_ = [('icon',w.BOOL),('x',w.DWORD),('y',w.DWORD),('mask',w.HBITMAP),('color',w.HBITMAP)]
class Bitmap(c.Structure):
    _fields_ = [('type',w.LONG),('width',w.LONG),('height',w.LONG),('stride',w.LONG),('planes',w.WORD),('bpp',w.WORD),('bits',c.c_void_p)]
icon_info = bind(user, 'GetIconInfo', [w.HANDLE,c.POINTER(IconInfo)], w.BOOL)
get_object = bind(gdi, 'GetObjectW', [w.HANDLE,c.c_int,c.c_void_p], c.c_int)
def dimensions(cursor):
    info=IconInfo(); assert icon_info(cursor,c.byref(info)); bitmap=Bitmap()
    try:
        assert get_object(info.color or info.mask,c.sizeof(bitmap),c.byref(bitmap))
        return bitmap.width, bitmap.height if info.color else bitmap.height//2, info.x, info.y
    finally:
        if info.color: delete(info.color)
        delete(info.mask)
def appearance(cursor):
    width,height,x,y=dimensions(cursor); dc=create_dc(None)
    header=Header(40,width,-height,1,32,0,0,0,0,0,0); bits=c.c_void_p()
    bitmap=create_dib(dc,c.byref(header),0,c.byref(bits),None,0); assert bitmap
    old=select(dc,bitmap); hashes=[]
    try:
        for background in (0,255):
            for step in range(3):
                c.memset(bits,background,width*height*4)
                assert draw(dc,0,0,cursor,width,height,step,None,3); flush()
                raw=c.string_at(bits,width*height*4)
                hashes.append(hashlib.sha256(bytes(v for i,v in enumerate(raw) if i%4!=3)).hexdigest())
        return [width,height,x,y,*hashes]
    finally:
        select(dc,old); delete(bitmap); delete_dc(dc)
def capture():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Control Panel\Cursors') as key:
        values={}
        for name in ROLES+['','Scheme Source']:
            try: values[name]=winreg.QueryValueEx(key,name)
            except FileNotFoundError: values[name]=None
        return values
def live():
    return {role:appearance(load(None,ident)) for role,ident in zip(ROLES,IDS)}
def command(mode):
    subprocess.run([str(EXE),mode],cwd=ROOT,check=True,timeout=10,creationflags=subprocess.CREATE_NO_WINDOW)
def send(message, wp=0):
    window=find('Hide.Native.v1',None); assert window
    result=c.c_size_t()
    assert send_message(window,message,wp,0,2,3000,c.byref(result))
    return result.value
def status():
    send(0x8006); return json.loads((ROOT/'state/status.json').read_text())
def wait_ready(supported=True):
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        info=status()
        if not info['preparing'] and info['supported']==supported: return info
        time.sleep(.025)
    raise AssertionError(('readiness timeout',info))
def launch():
    process=subprocess.Popen([str(EXE),'--backend-test'],cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    deadline=time.monotonic()+5
    while not find('Hide.Native.v1',None):
        assert process.poll() is None and time.monotonic()<deadline; time.sleep(.025)
    wait_ready(); return process

assert not find('Hide.Native.v1',None), 'Exit Hide before this diagnostic'
initial=capture(); baseline=live(); process=None
report={'date':'2026-10-05','passed':False,'cycles':args.cycles,'checks':[],'preference_writes':False,'input_injected':False}
try:
    command('--recover')
    assert capture()==initial and live()==baseline, 'Explicit recovery changed the current skin'
    report['checks'].append('recovery preserves all 17 displayed roles, sampled animation frames and hotspots')
    process=launch(); assert capture()==initial and live()==baseline
    for _ in range(15):
        assert send(0x800a)==1 and send(0x800b)==1
    time.sleep(.5)
    before=status(); latencies=[]; handle_samples=[]
    for i in range(args.cycles):
        assert send(0x800a)==1
        assert send(0x800b)==1
        assert capture()==initial and live()==baseline, ('restored appearance differs',i)
        sample=status(); latencies.append(sample['last_fade_ms'])
        if i%10==0:handle_samples.append({name:sample[name] for name in ['handles','gdi_handles','user_handles']})
    after=status()
    report['cycles_before']=before; report['cycles_after']=after; report['handle_samples']=handle_samples
    # UI Automation can initialize a bounded set of handles asynchronously. Check the
    # later half for continuing growth and independently bound drawing/window resources.
    midpoint=handle_samples[len(handle_samples)//2]
    for name in ['handles','gdi_handles','user_handles']:
        assert after[name]<=midpoint[name]+4, (name,midpoint[name],after[name])
    for name in ['gdi_handles','user_handles']:
        assert after[name]<=before[name]+4, (name,before[name],after[name])
    report['fade_p95_ms']=sorted(latencies)[max(0,int(len(latencies)*.95)-1)]
    report['checks'].append('every fade/restore keeps registry and actual displayed skin identical')
    source=os.path.expandvars(initial['IBeam'][0]); assert source
    width,height,_,_=dimensions(load(None,32512))
    override=load_file(None,source,2,width,height,16); assert override
    assert set_cursor(override,32512)
    overridden=appearance(load(None,32512)); assert overridden!=baseline['Arrow']
    assert send(0x800a)==0, 'Mismatch protection failed'
    wait_ready(False)
    assert appearance(load(None,32512))==overridden and capture()==initial, 'Rejected fade still switched skin'
    report['checks'].append('unannounced live skin mismatch rejects fading without replacing the visible pointer')
    command('--recover'); send(0x111,241); wait_ready()
    assert live()==baseline and capture()==initial
    assert send(0x800a)==1
    process.kill(); process.wait(timeout=5); process=None
    deadline=time.monotonic()+5
    while (ROOT/'state/active.lock').exists() and time.monotonic()<deadline:time.sleep(.05)
    assert not (ROOT/'state/active.lock').exists() and capture()==initial and live()==baseline
    report['checks'].append('guardian restores exact current skin after forced termination')
    process=launch(); assert live()==baseline
    assert send(0x800a)==1; send(0x10); process.wait(timeout=5); process=None
    assert capture()==initial and live()==baseline
    report['checks'].append('restart and exit while faded preserve actual appearance')
    report['passed']=True
finally:
    if process is not None:
        try:send(0x10); process.wait(timeout=5)
        except Exception:process.kill();process.wait(timeout=5);time.sleep(1)
    command('--recover')
    report['final_settings_preserved']=capture()==initial
    report['final_appearance_preserved']=live()==baseline
    assert report['final_settings_preserved'] and report['final_appearance_preserved']
    (ROOT/args.output).write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
