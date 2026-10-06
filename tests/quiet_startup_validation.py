"""Interactive Windows regression for quiet settings and task-managed recovery.

Exit Hide first. Refuses to replace an existing Hide scheduled task. Temporarily
migrates an existing Run entry, then restores it in finally. Sends no text outside
the native Backspace test's own controlled window. Reports only aggregate results.
"""
import base64
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import time
import winreg
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/Hide.exe'
STATE = EXE.parent / 'state'
REPORT = ROOT / 'state/quiet-startup-validation.json'
PS = r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
user = c.WinDLL('user32', use_last_error=True)
kernel = c.WinDLL('kernel32', use_last_error=True)
user.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
user.FindWindowW.restype = w.HWND
user.IsWindowVisible.argtypes = [w.HWND]
user.IsWindowVisible.restype = w.BOOL
user.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
user.SendMessageTimeoutW.restype = c.c_ssize_t
kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenProcess.restype = w.HANDLE
kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
kernel.TerminateProcess.restype = w.BOOL
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.CloseHandle.restype = w.BOOL

def ps(script):
    text = "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new(); $ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue';\n" + script
    encoded = base64.b64encode(text.encode('utf-16-le')).decode()
    result = subprocess.run([PS, '-NoProfile', '-NonInteractive', '-EncodedCommand', encoded],
        capture_output=True, encoding='utf-8', check=True, timeout=25, creationflags=subprocess.CREATE_NO_WINDOW)
    return result.stdout.strip()

def command(mode, *arguments, accepted=(0,)):
    result = subprocess.run([str(EXE), mode, *map(str, arguments)], cwd=EXE.parent,
        timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode in accepted, (mode, result.returncode)
    return result.returncode

def send(message, wp=0, window=None):
    window = window or user.FindWindowW('Hide.Native.v1', None)
    assert window
    result = c.c_size_t()
    assert user.SendMessageTimeoutW(window, message, wp, 0, 2, 3000, c.byref(result))
    return result.value

def status():
    send(0x8006)
    return json.loads((STATE / 'status.json').read_text())

def wait(predicate, seconds=12):
    limit = time.monotonic() + seconds
    while time.monotonic() < limit:
        if predicate(): return
        time.sleep(.1)
    raise AssertionError('condition timed out')

def ready():
    if not user.FindWindowW('Hide.Native.v1', None): return False
    value = status()
    return value['hooks_ready'] and value['guardian_alive'] and value['supported'] and not value['preparing']

def stop():
    if user.FindWindowW('Hide.Native.v1', None): command('--exit')
    wait(lambda: not user.FindWindowW('Hide.Native.v1', None))
    wait(lambda: not (STATE / 'mouse-vanish.lock').exists())

def kill(pid):
    handle = kernel.OpenProcess(1, False, pid)
    assert handle
    try: assert kernel.TerminateProcess(handle, 99)
    finally: kernel.CloseHandle(handle)

def config():
    result = {}
    for path in [r'Control Panel\Cursors', r'Control Panel\Mouse']:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
            values = {}; index = 0
            while True:
                try: name, value, kind = winreg.EnumValue(key, index)
                except OSError: break
                values[name] = (value, kind); index += 1
            result[path] = values
    return result

assert not user.FindWindowW('Hide.Native.v1', None), 'Exit the existing Hide instance first'
task_name = 'Hide-' + ps('[System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value')
assert ps(f"[bool](Get-ScheduledTask -TaskName '{task_name}' -ErrorAction SilentlyContinue)") == 'False', 'An existing Hide task must be preserved; use an isolated test account'
run_path = r'Software\Microsoft\Windows\CurrentVersion\Run'
with winreg.OpenKey(winreg.HKEY_CURRENT_USER, run_path) as key:
    try: original_run = winreg.QueryValueEx(key, 'Hide')
    except FileNotFoundError: original_run = None
original_config = config()
checks = []
report = {'passed': False, 'date': '2026-10-07', 'checks': checks}
STATE.mkdir(exist_ok=True)
settings_path = STATE / 'settings.ini'
original_settings = settings_path.read_bytes() if settings_path.exists() else None
write_profile = user.WritePrivateProfileStringW if hasattr(user, 'WritePrivateProfileStringW') else kernel.WritePrivateProfileStringW
write_profile.argtypes = [w.LPCWSTR, w.LPCWSTR, w.LPCWSTR, w.LPCWSTR]
write_profile.restype = w.BOOL
assert write_profile('Settings', 'ShowTray', '0', str(settings_path))

def mark(text):
    checks.append(text)
    print('PASS: ' + text, flush=True)

try:
    command('--enable-startup'); command('--enable-startup')
    xml = ps(f"Export-ScheduledTask -TaskName '{task_name}'")
    task = ET.fromstring(xml)
    ns = {'t': 'http://schemas.microsoft.com/windows/2004/02/mit/task'}
    def value(path): return task.find('t:' + path.replace('/', '/t:'), ns).text
    assert value('Principals/Principal/LogonType') == 'InteractiveToken'
    # Task Scheduler may omit the default LeastPrivilege element when exporting.
    assert ps(f"(Get-ScheduledTask -TaskName '{task_name}').Principal.RunLevel.ToString()") == 'Limited'
    assert value('Settings/ExecutionTimeLimit') == 'PT0S'
    assert value('Settings/RestartOnFailure/Count') == '3'
    assert value('Settings/RestartOnFailure/Interval') == 'PT1M'
    assert value('Settings/DisallowStartIfOnBatteries') == 'false'
    assert value('Settings/StopIfGoingOnBatteries') == 'false'
    assert value('Triggers/LogonTrigger/Delay') == 'PT10S'
    assert Path(value('Actions/Exec/Command')) == EXE
    assert value('Actions/Exec/Arguments') == '--supervise'
    assert value('Triggers/TimeTrigger/Repetition/Interval') == 'PT1M'
    assert value('Actions/Exec/WorkingDirectory') == str(EXE.parent)
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, run_path) as key:
        try: winreg.QueryValueEx(key, 'Hide'); raise AssertionError('old Run entry remains')
        except FileNotFoundError: pass
    mark('idempotent registration; interactive least-privilege task; bounded retries; no battery/runtime limit; old Run migrated')
    ps(f"Start-ScheduledTask -TaskName '{task_name}'")
    wait(ready)
    baseline = status(); pid = baseline['pid']
    assert not baseline['show_tray'] and not baseline['tray_added'] and not baseline['settings_open']
    assert baseline['startup_backend'] == 'task' and baseline['startup']
    mark('task-managed quiet launch; input hooks, skin and guardian ready; no tray or settings window')
    for _ in range(3):
        command('--settings')
        wait(lambda: bool(user.FindWindowW('Hide.Settings.v1', None)))
        assert user.IsWindowVisible(user.FindWindowW('Hide.Settings.v1', None))
        assert status()['pid'] == pid and status()['settings_open']
        send(0x10, window=user.FindWindowW('Hide.Settings.v1', None))
        assert status()['running'] and not status()['settings_open']
    first = status()
    for _ in range(25):
        send(0x800C)
        assert user.FindWindowW('Hide.Settings.v1', None)
        send(0x10, window=user.FindWindowW('Hide.Settings.v1', None))
    after = status()
    assert after['pid'] == pid
    assert after['gdi_handles'] <= first['gdi_handles'] + 2
    assert after['user_handles'] <= first['user_handles'] + 2
    report['settings_resource_samples'] = {name: [first[name], after[name]] for name in ['private_bytes', 'working_set_bytes', 'handles', 'gdi_handles', 'user_handles']}
    mark('repeat launch opens existing settings; closing settings keeps engine; 25 open/close cycles without GUI handle growth')
    command('--show-tray'); assert status()['show_tray'] and status()['tray_added']
    command('--hide-tray'); assert not status()['show_tray'] and not status()['tray_added']
    command('--pause'); assert not status()['enabled'] and not (STATE / 'mouse-vanish.lock').exists()
    send(0x111, 100); assert status()['enabled']
    mark('live tray toggle and pause/resume work in quiet mode')
    ps(f"Disable-ScheduledTask -TaskName '{task_name}' | Out-Null")
    wait(lambda: not status()['startup'], seconds=8)
    command('--settings')
    send(0x10, window=user.FindWindowW('Hide.Settings.v1', None))
    assert 'False' == ps(f"(Get-ScheduledTask -TaskName '{task_name}').Settings.Enabled")
    mark('opening settings does not re-enable a task disabled by the user')
    ps(f"Enable-ScheduledTask -TaskName '{task_name}' | Out-Null")
    # Disabling a running task can cancel that instance's pending failure policy.
    # Start a fresh instance after re-enabling to validate the registered policy.
    stop()
    wait(lambda: ps(f"(Get-ScheduledTask -TaskName '{task_name}').State.ToString()") == 'Ready')
    ps(f"Start-ScheduledTask -TaskName '{task_name}'")
    wait(ready)
    old_pid = status()['pid']
    kill(old_pid)
    wait(lambda: not (STATE / 'mouse-vanish.lock').exists())
    print('Waiting for native supervised failure recovery...', flush=True)
    wait(lambda: ready() and status()['pid'] != old_pid, seconds=15)
    assert not status()['settings_open'] and not status()['tray_added']
    mark('forced main termination recovered cursor and supervisor restarted a healthy quiet instance')
    guard_pid = status()['guardian_pid']
    kill(guard_pid)
    wait(lambda: not user.FindWindowW('Hide.Native.v1', None))
    wait(lambda: not (STATE / 'mouse-vanish.lock').exists())
    assert json.loads((STATE / 'status.json').read_text())['exit_code'] == 11
    mark('guardian loss returns explicit failure after restoring preferences')
    print('Waiting for scheduled health check after supervisor loss...', flush=True)
    wait(ready, seconds=90)
    assert status()['guardian_pid'] != guard_pid
    mark('supervisor loss recovered by periodic Windows health check')
    stop()
    wait(lambda: ps(f"(Get-ScheduledTask -TaskName '{task_name}').State.ToString()") == 'Ready')
    assert ps(f"(Get-ScheduledTaskInfo -TaskName '{task_name}').LastTaskResult") == '0'
    final_xml=ET.fromstring(ps(f"Export-ScheduledTask -TaskName '{task_name}'"))
    health_trigger=final_xml.find('t:Triggers/t:TimeTrigger/t:Enabled',ns)
    assert health_trigger is not None and health_trigger.text=='false'
    assert ps(f"(Get-ScheduledTask -TaskName '{task_name}').Settings.Enabled")=='True'
    mark('explicit exit pauses health check while preserving next-logon startup')
    command('--disable-startup'); command('--disable-startup')
    assert ps(f"[bool](Get-ScheduledTask -TaskName '{task_name}' -ErrorAction SilentlyContinue)") == 'False'
    command('--status', accepted=(2,))
    assert not json.loads((STATE / 'status.json').read_text())['running']
    mark('normal exit returns zero; repeated disable removes task; offline status cannot report stale online state')
    command('--backspace-test', 75)
    backspace = json.loads((STATE / 'integration-result.json').read_text())
    assert backspace['passed']
    report['backspace_integration'] = backspace
    wait(lambda: not (STATE / 'mouse-vanish.lock').exists())
    assert config() == original_config
    mark('visible 75% Backspace fading and mouse restoration; persistent cursor/mouse preferences preserved')
    report['passed'] = True
finally:
    try: stop()
    finally:
        command('--disable-startup')
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, run_path, 0, winreg.KEY_SET_VALUE) as key:
            if original_run is not None: winreg.SetValueEx(key, 'Hide', 0, original_run[1], original_run[0])
            else:
                try: winreg.DeleteValue(key, 'Hide')
                except FileNotFoundError: pass
        report['persistent_preferences_preserved'] = config() == original_config
        if original_settings is not None: settings_path.write_bytes(original_settings)
        else: settings_path.unlink(missing_ok=True)
        REPORT.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report), flush=True)
assert report['passed'] and report['persistent_preferences_preserved']
