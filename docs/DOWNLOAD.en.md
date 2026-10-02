# Download and run Hide

Hide runs directly on Windows 10/11 x64. It does not require Codex, a GitHub client, Python, or .NET.

**This beta requires the exact tested Ikaros cursor files to be installed and selected beforehand.** It checks file hashes; another skin or a different Ikaros version will be rejected. It does not install a cursor skin for you.

1. [Download Hide-windows-x64.zip](https://github.com/DeriitoMe/Hide/releases/download/v1.0.0-beta.1/Hide-windows-x64.zip), or open the [release page](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0-beta.1) and select that file under Assets. Public downloads work in a browser.
2. Extract the entire ZIP into a writable folder.
3. Open the extracted Hide folder and double-click Hide.exe or Start-Hide.cmd. Keep resources and the other files beside the executable.
4. Find Hide in the Windows system tray; expand hidden tray icons if needed. The menu is currently in Chinese.
5. Start typing in an editable field. The cursor becomes 75% transparent by default, then returns when you move it about 3 screen pixels, click, scroll, or stop typing for about 1.5 seconds.

Right-click the tray icon for transparency, hide, pause, idle restoration, per-app detection, and startup options. Startup is off by default.

Ctrl+Alt+F12 immediately restores the cursor and pauses Hide. Use the tray menu to enable it again. Exit with the tray's “退出并恢复皮肤” command. If needed, run Recover-Cursor.cmd to pause Hide and reload the normal cursor scheme.

Before upgrading, moving, or deleting the folder, exit normally. Before uninstalling, disable startup if you enabled it.

The Assets file named Hide-windows-x64.zip contains the ready-to-run application. Source code (zip) and Source code (tar.gz) contain source for developers.

Optional integrity check: download SHA256SUMS.txt from the same release, run Get-FileHash .\Hide-windows-x64.zip -Algorithm SHA256 in PowerShell, and compare the hash. This build is not code-signed.

This is a prerelease. Long-running use, sleep/lock cycles, IME workflows, and compatibility across applications need further testing. [Validation report](VALIDATION.md) and [issues](https://github.com/DeriitoMe/Hide/issues).
