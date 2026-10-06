"""Interactive Windows regression test. Restores exact cursor values in finally; no text injection."""
from pathlib import Path
import ctypes as c
from ctypes import wintypes as w
import json
import os
import shutil
import subprocess
import time
import winreg
import argparse

parser=argparse.ArgumentParser()
parser.add_argument('--cycles',type=int,default=1000)
parser.add_argument('--output',default='evidence/dynamic-scheme-desktop.json')
args=parser.parse_args()
assert args.cycles>=25

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'Hide.exe'
ROLES = ['Arrow','Help','AppStarting','Wait','Crosshair','IBeam','NWPen','No','SizeNS','SizeWE','SizeNWSE','SizeNESW','SizeAll','UpArrow','Hand','Pin','Person']
NAMES = ROLES + ['', 'Scheme Source']
KEY = r'Control Panel\Cursors'
user = c.WinDLL('user32', use_last_error=True)
user.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
user.FindWindowW.restype = w.HWND
user.SendMessageTimeoutW.argtypes = [w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)]
user.SendMessageTimeoutW.restype = c.c_ssize_t
user.SystemParametersInfoW.argtypes = [w.UINT,w.UINT,c.c_void_p,w.UINT]
user.SystemParametersInfoW.restype = w.BOOL
user.LoadImageW.argtypes = [w.HINSTANCE,w.LPCWSTR,w.UINT,c.c_int,c.c_int,w.UINT]
user.LoadImageW.restype = w.HANDLE
user.SetSystemCursor.argtypes = [w.HANDLE,w.DWORD]
user.SetSystemCursor.restype = w.BOOL
user.DestroyCursor.argtypes = [w.HANDLE]
user.DestroyCursor.restype = w.BOOL
IDS=[32512,32651,32650,32514,32515,32513,32631,32648,32645,32644,32642,32643,32646,32516,32649,32671,32672]

def capture():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY) as key:
        result = {}
        for name in NAMES:
            try: result[name] = winreg.QueryValueEx(key, name)
            except FileNotFoundError: result[name] = None
        return result

def install(values, reload=True):
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,KEY,0,winreg.KEY_SET_VALUE) as key:
        for name, value in values.items():
            if value is None:
                try: winreg.DeleteValue(key,name)
                except FileNotFoundError: pass
            else: winreg.SetValueEx(key,name,0,value[1],value[0])
    if reload:
        assert user.SystemParametersInfoW(0x57,0,None,0), 'system cursor reload failed'
        # Mouse Properties applies custom role files explicitly. SPI_SETCURSORS
        # alone may leave Person stale; reproduce a complete user application.
        for role,ident in zip(ROLES,IDS):
            entry=values[role]
            if not entry or not entry[0]:continue
            cursor=user.LoadImageW(None,os.path.expandvars(entry[0]),2,
                                  user.GetSystemMetrics(13),user.GetSystemMetrics(14),0x10)
            assert cursor,role
            if not user.SetSystemCursor(cursor,ident):
                user.DestroyCursor(cursor);raise AssertionError(('role apply failed',role))

def send(message):
    window=user.FindWindowW('Hide.Native.v1',None)
    assert window, 'Hide window not available'
    result=c.c_size_t()
    assert user.SendMessageTimeoutW(window,message,0,0,2,3000,c.byref(result)), 'Hide did not respond'
    return result.value

def status():
    send(0x8006)
    return json.loads((ROOT/'state/status.json').read_text())

def wait_ready(expected=True):
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        info=status()
        if not info['preparing'] and info['supported']==expected:return info
        time.sleep(.025)
    raise AssertionError(('scheme did not become ready',info))

def fade(): assert send(0x800a)==1, 'fade failed'
def restore(): assert send(0x800b)==1, 'restore failed'
def launch():
    p=subprocess.Popen([str(EXE),'--backend-test'],cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    deadline=time.monotonic()+4
    while not user.FindWindowW('Hide.Native.v1',None):
        assert p.poll() is None
        assert time.monotonic()<deadline
        time.sleep(.025)
    wait_ready()
    return p

def check_values(expected):
    actual=capture()
    assert actual==expected, 'a transition overwrote the selected scheme'
    assert not any(value and 'cache-v2' in str(value[0]) for value in actual.values()), 'private cache leaked into settings'

initial=capture()
assert not user.FindWindowW('Hide.Native.v1',None), 'exit Hide before running this test'
results={'date':'2026-10-07','passed':False,'checks':[],'input_injected':False}
process=None
source_copy=ROOT/'build/source-replacement.ani'
try:
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,KEY+r'\Schemes') as key:
        saved=winreg.QueryValueEx(key,'Default')[0].split(',')
    assert len(saved)==17
    schemes={
        'Ikaros': {**initial,'':('Hide test Ikaros',winreg.REG_SZ)},
        'Default': {**initial,**{role:(path,winreg.REG_SZ) for role,path in zip(ROLES,saved)},'':('Hide test Default',winreg.REG_SZ)},
        'System default empty paths': {**initial,**{role:('',winreg.REG_SZ) for role in ROLES},'':('',winreg.REG_SZ)}
    }
    process=launch()
    for name,values in schemes.items():
        install(values);time.sleep(.15);info=wait_ready();fade();check_values(values);restore();check_values(values)
        results['checks'].append({'case':name,'passed':True,'fade_ms':status()['last_fade_ms']})
    install(schemes['Ikaros']);time.sleep(.15);wait_ready();fade()
    install(schemes['Default']);time.sleep(.15);wait_ready();check_values(schemes['Default']);fade();restore();check_values(schemes['Default'])
    results['checks'].append({'case':'switch while faded, then fade/restore new scheme','passed':True})
    for _ in range(34):
        for values in schemes.values():install(values)
    time.sleep(.15);wait_ready();check_values(schemes['System default empty paths']);fade();restore()
    results['checks'].append({'case':'102 rapid scheme changes, latest wins','passed':True})
    mixed={**schemes['Ikaros'],'IBeam':schemes['Default']['IBeam']}
    install(mixed);time.sleep(.15);wait_ready();fade();restore();check_values(mixed)
    results['checks'].append({'case':'mixed skin, only IBeam changed','passed':True})
    shutil.copyfile(os.path.expandvars(schemes['Ikaros']['Arrow'][0]),source_copy)
    replaced={**mixed,'Arrow':(str(source_copy),winreg.REG_SZ)}
    install(replaced);time.sleep(.15);before=wait_ready();fade();restore()
    shutil.copyfile(os.path.expandvars(schemes['Default']['Arrow'][0]),source_copy)
    install(replaced)
    time.sleep(.15);after=wait_ready();assert after['scheme_changes']>before['scheme_changes'];fade();restore();check_values(replaced)
    results['checks'].append({'case':'same filename source replaced','passed':True})
    source_copy.write_bytes(b'bad cursor')
    time.sleep(.15);wait_ready(False);assert send(0x800a)==0;check_values(replaced)
    results['checks'].append({'case':'malformed source preserves configuration and rejects fading','passed':True})
    install(schemes['Default']);time.sleep(.15);wait_ready();fade();restore();check_values(schemes['Default'])
    before=status()
    latencies=[]
    for i in range(args.cycles):
        fade();restore()
        if i%25==0:
            latencies.append(status()['last_fade_ms']);check_values(schemes['Default']);time.sleep(.005)
    after=status()
    for name in ['handles','gdi_handles','user_handles']:assert after[name]<=before[name]+4,(name,before[name],after[name])
    results['stress']={'cycles':args.cycles,'before':before,'after':after,'sampled_fade_p95_ms':sorted(latencies)[max(0,int(len(latencies)*.95)-1)]}
    results['checks'].append({'case':f'{args.cycles} live fade/restore cycles, no persistent handle growth','passed':True})
    fade();install(schemes['Ikaros'],reload=False)
    process.kill();assert process.wait(timeout=5)!=0;process=None
    time.sleep(1.5);check_values(schemes['Ikaros'])
    assert not (ROOT/'state/active.lock').exists(), 'guardian did not complete recovery'
    results['checks'].append({'case':'forced termination after selecting new scheme, guardian preserves new choice','passed':True})
    process=launch();check_values(schemes['Ikaros']);fade();restore();check_values(schemes['Ikaros'])
    results['checks'].append({'case':'restart keeps latest scheme','passed':True})
    install(schemes['Default']);time.sleep(.15);wait_ready();fade();send(0x10);process.wait(timeout=5);process=None;check_values(schemes['Default'])
    results['checks'].append({'case':'exit while faded keeps newly selected scheme','passed':True})
    results['passed']=True
finally:
    if process is not None:
        try:send(0x10);process.wait(timeout=5)
        except Exception:process.kill();process.wait(timeout=5);time.sleep(1.5)
    install(initial)
    subprocess.run([str(EXE),'--recover'],cwd=ROOT,check=True,timeout=10,creationflags=subprocess.CREATE_NO_WINDOW)
    check_values(initial)
    source_copy.unlink(missing_ok=True)
    (ROOT/args.output).write_text(json.dumps(results,indent=2),encoding='utf-8')
print(json.dumps(results,indent=2))
