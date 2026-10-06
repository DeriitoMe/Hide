"""Interactive first-launch test. Exit Hide and remove its test task first.

Temporarily resets only build/state/settings.ini and restores the desktop link,
settings and startup registration afterward. Does not inject user input.
"""
import ctypes as c
from ctypes import wintypes as w
import base64,json,subprocess,time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build/Hide.exe'
STATE=EXE.parent/'state'
PS=r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
u=c.WinDLL('user32')
u.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR];u.FindWindowW.restype=w.HWND
u.IsWindowVisible.argtypes=[w.HWND];u.IsWindowVisible.restype=w.BOOL
u.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)]
u.SendMessageTimeoutW.restype=c.c_ssize_t
def ps(script):
    script="[Console]::OutputEncoding=[Text.UTF8Encoding]::new();$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';\n"+script
    encoded=base64.b64encode(script.encode('utf-16-le')).decode()
    return subprocess.check_output([PS,'-NoProfile','-NonInteractive','-EncodedCommand',encoded],encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW,timeout=20).strip()
def command(mode):
    subprocess.run([str(EXE),mode],cwd=EXE.parent,check=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=10)
def send(message,wp=0,window=None):
    result=c.c_size_t();assert u.SendMessageTimeoutW(window or u.FindWindowW('Hide.Native.v1',None),message,wp,0,2,3000,c.byref(result));return result.value
def status():send(0x8006);return json.loads((STATE/'status.json').read_text())
def wait(predicate):
    limit=time.monotonic()+12
    while time.monotonic()<limit:
        if predicate():return
        time.sleep(.05)
    raise AssertionError('first launch did not become ready')
assert not u.FindWindowW('Hide.Native.v1',None)
task='Hide-'+ps('[Security.Principal.WindowsIdentity]::GetCurrent().User.Value')
assert ps(f"[bool](Get-ScheduledTask -TaskName '{task}' -ErrorAction SilentlyContinue)")=='False'
settings=STATE/'settings.ini';previous=settings.read_bytes() if settings.exists() else None
desktop=Path(ps("[Environment]::GetFolderPath('Desktop')"));shortcut=desktop/'Hide 设置.lnk'
old_shortcut=shortcut.read_bytes() if shortcut.exists() else None
report={'passed':False,'date':'2026-10-07','checks':[]}
try:
    settings.unlink(missing_ok=True)
    command('--settings')
    wait(lambda:bool(u.FindWindowW('Hide.Native.v1',None)))
    wait(lambda:status()['supported'] and status()['hooks_ready'] and status()['guardian_alive'])
    first=status();window=u.FindWindowW('Hide.Settings.v1',None)
    assert first['show_tray'] and first['tray_added'] and first['startup']
    assert window and u.IsWindowVisible(window)
    report['checks'].append('fresh install shows original tray and visible guide; current-user sign-in startup enabled')
    send(0x111,243)
    assert not status()['show_tray'] and not status()['tray_added'] and status()['startup']
    assert shortcut.exists()
    shortcut_info=json.loads(ps("$link=(New-Object -ComObject WScript.Shell).CreateShortcut([IO.Path]::Combine([Environment]::GetFolderPath('Desktop'),'Hide 设置.lnk'));[PSCustomObject]@{target=$link.TargetPath;arguments=$link.Arguments;hotkey=$link.Hotkey;icon=$link.IconLocation}|ConvertTo-Json"))
    assert Path(shortcut_info['target'])==EXE and shortcut_info['arguments']=='--settings'
    assert set(shortcut_info['hotkey'].lower().split('+'))=={'ctrl','alt','h'}
    assert shortcut_info['icon'].lower().startswith(str(EXE).lower())
    send(0x10,window=window);assert not status()['settings_open'] and status()['running']
    report['checks'].append('choosing quiet keeps engine and startup; desktop link and Ctrl+Alt+H point to existing settings')
    pid=status()['pid'];command('--settings');assert status()['pid']==pid
    send(0x10,window=u.FindWindowW('Hide.Settings.v1',None));command('--exit')
    wait(lambda:not u.FindWindowW('Hide.Native.v1',None))
    wait(lambda:ps(f"(Get-ScheduledTask -TaskName '{task}').State.ToString()")=='Ready')
    ps(f"Start-ScheduledTask -TaskName '{task}'")
    wait(lambda:bool(u.FindWindowW('Hide.Native.v1',None)))
    wait(lambda:status()['supported'] and status()['hooks_ready'])
    assert not status()['show_tray'] and not status()['settings_open'] and status()['startup']
    report['checks'].append('quiet preference survives task launch; existing instance reused; settings close differs from exit')
    report['passed']=True
finally:
    if u.FindWindowW('Hide.Native.v1',None):command('--exit')
    wait(lambda:not u.FindWindowW('Hide.Native.v1',None))
    command('--disable-startup')
    if previous is None:settings.unlink(missing_ok=True)
    else:settings.write_bytes(previous)
    if old_shortcut is None:shortcut.unlink(missing_ok=True)
    else:shortcut.write_bytes(old_shortcut)
    (ROOT/'state/onboarding-validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report),flush=True)
assert report['passed']
