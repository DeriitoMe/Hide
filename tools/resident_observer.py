"""Local development observation. Not required or bundled for Hide users.

Polls aggregate status and resources. Does not inject input, restart the app or
collect key values/text. Exit or a restart is reported as an interruption.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--hours', type=float, default=8)
parser.add_argument('--interval', type=float, default=60)
args = parser.parse_args()
assert 0 < args.hours <= 24 and args.interval >= 10
ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / 'state'
EXE = ROOT / 'Hide.exe'
OUTPUT = STATE / 'quiet-resident-observation.json'
kernel = c.WinDLL('kernel32', use_last_error=True)
kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenProcess.restype = w.HANDLE
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.GetProcessTimes.argtypes = [w.HANDLE, c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME)]
kernel.GetProcessTimes.restype = w.BOOL

def cpu_seconds(pid):
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle: return None
    created, ended, system, user = [w.FILETIME() for _ in range(4)]
    try:
        if not kernel.GetProcessTimes(handle, c.byref(created), c.byref(ended), c.byref(system), c.byref(user)): return None
        return ((system.dwHighDateTime << 32) + system.dwLowDateTime + (user.dwHighDateTime << 32) + user.dwLowDateTime) / 10_000_000
    finally: kernel.CloseHandle(handle)

def sample():
    result = subprocess.run([str(EXE), '--status'], cwd=ROOT, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode: return None
    info = json.loads((STATE / 'status.json').read_text())
    return {key: info.get(key) for key in ['pid', 'guardian_pid', 'running', 'sample_utc', 'hooks_ready', 'guardian_alive', 'enabled', 'supported', 'preparing', 'transparency', 'keyboard_events', 'text_events', 'mouse_events', 'fade_count', 'restore_count', 'last_fade_ms', 'max_fade_ms', 'queue_ms', 'private_bytes', 'working_set_bytes', 'guardian_private_bytes', 'guardian_working_set_bytes', 'handles', 'gdi_handles', 'user_handles', 'show_tray', 'settings_open']}

start = time.monotonic()
report = {'state': 'running', 'started_utc': datetime.now(timezone.utc).isoformat(), 'planned_hours': args.hours,
          'samples': [], 'typed_content_collected': False, 'observer_required_for_product': False}
try:
    first = sample()
    assert first and first['running'] and first['hooks_ready'] and first['guardian_alive']
    pid = first['pid']
    guard_pid = first['guardian_pid']
    assert guard_pid
    initial_cpu = (cpu_seconds(pid) or 0) + (cpu_seconds(guard_pid) or 0)
    while True:
        info = sample()
        elapsed = time.monotonic() - start
        if not info or info['pid'] != pid or not info['hooks_ready'] or not info['guardian_alive']:
            report['state'] = 'interrupted'; report['reason'] = 'engine exited, restarted or lost monitoring'; break
        total_cpu = (cpu_seconds(pid) or 0) + (cpu_seconds(guard_pid) or 0)
        info['elapsed_seconds'] = round(elapsed, 3)
        info['combined_private_bytes'] = info['private_bytes'] + info['guardian_private_bytes']
        info['combined_working_set_bytes'] = info['working_set_bytes'] + info['guardian_working_set_bytes']
        info['average_machine_cpu_percent'] = round(max(0, total_cpu - initial_cpu) / max(elapsed, 1) / os.cpu_count() * 100, 4)
        report['samples'].append(info)
        report['elapsed_seconds'] = round(elapsed, 3)
        if elapsed >= args.hours * 3600:
            report['state'] = 'completed'; report['ended_utc'] = datetime.now(timezone.utc).isoformat()
        temporary = OUTPUT.with_suffix('.tmp')
        temporary.write_text(json.dumps(report, indent=2), encoding='utf-8')
        temporary.replace(OUTPUT)
        if report['state'] != 'running': break
        time.sleep(min(args.interval, max(.1, args.hours * 3600 - elapsed)))
except Exception as error:
    report['state'] = 'interrupted'; report['reason'] = type(error).__name__ + ': ' + str(error)
finally:
    report['elapsed_seconds'] = round(time.monotonic() - start, 3)
    OUTPUT.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({key: value for key, value in report.items() if key != 'samples'}))
