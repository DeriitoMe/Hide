# Download and run Hide

Hide runs on Windows 10/11 x64 without Codex, a GitHub client, Python, .NET, or a mouse driver.

1. [Download Hide-windows-x64.zip](https://github.com/DeriitoMe/Hide/releases/download/v1.0.0/Hide-windows-x64.zip), or open the [release page](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0) and choose that file under **Assets**.
2. Extract the ZIP into a writable folder.
3. Open the Hide folder and double-click **Hide.exe** or **Start-Hide.cmd**.
4. Find Hide in the Windows system tray; expand hidden icons if needed. The tray menu is currently in Chinese.
5. Type in an editable field. Your cursor becomes 75% transparent by default. Moving about 3 screen pixels, clicking, scrolling, or stopping typing for about 1.5 seconds restores it.

Hide follows your current Windows cursor scheme. Apply a new scheme in Windows Mouse Properties and it will prepare matching transparent cursors locally. Common static CUR and animated ANI files are supported. Your original files remain intact; the portable package uses your installed skin.

Right-click the tray for transparency, complete hiding, pause, idle restoration, per-app detection and startup. Startup is off by default. “重新识别当前皮肤” rescans the current skin.

Backspace and Ctrl+Backspace also start fading at your selected transparency; repeated deletion keeps the cursor faded. While enabled, Hide temporarily turns off Windows “Hide pointer while typing” so partial transparency stays visible. Pause, exit and recovery restore its previous state without changing the saved Windows preference.

To start Hide at sign-in, check **随 Windows 启动** in the tray menu; uncheck it to disable. Keep the folder in place. After moving it, enable startup from the new location to update the path. Hide.exe --enable-startup and --disable-startup are also available and do not require administrator privileges.

Before fading, Hide compares the configured skin with the displayed pointer images and hotspots. If they differ, fading is paused and the visible skin is preserved. Reapply your intended scheme in Windows Mouse Properties and rescan. Restoration explicitly reloads the latest custom role files, including roles that the Windows reload call may leave stale. Hide does not automatically choose a saved named scheme.

**Ctrl+Alt+F12** immediately restores and pauses Hide. Enable it again through the tray. Exit with “退出并恢复皮肤”. For recovery, run **Recover-Cursor.cmd**. A separate guardian reloads your latest cursor settings after an unexpected termination.

Exit before upgrading or moving the folder. Before uninstalling, disable startup if enabled. Settings are stored in state/settings.ini. If an older version left a scheme label inconsistent with its actual cursor paths, apply your intended scheme once in Windows Mouse Properties.

The ready-to-run file is **Hide-windows-x64.zip**. GitHub's **Source code** archives are for developers. Optional SHA256SUMS.txt is on the same release page; compare it with Get-FileHash .\Hide-windows-x64.zip -Algorithm SHA256. This build is unsigned.

Compatibility is based on tested formats and samples. Complex colored XOR, compressed bitmap formats, oversized resources and system animations with unavailable full sources may not support partial fading. Unsupported schemes keep their normal appearance; complete hiding may work. Application-drawn cursors, games, elevated windows and protected desktops have separate limitations.

Long-term use, sleep/lock cycles, IME workflows and compatibility across applications need further testing. [Validation report](VALIDATION.md) · [Issues](https://github.com/DeriitoMe/Hide/issues).
