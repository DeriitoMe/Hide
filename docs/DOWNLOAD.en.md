# Download and run Hide

Hide runs on Windows 10/11 x64 without Codex, a GitHub client, Python, .NET, or a mouse driver.

1. [Download Hide-windows-x64.zip](https://github.com/DeriitoMe/Hide/releases/download/v1.1.0/Hide-windows-x64.zip), or open the [release page](https://github.com/DeriitoMe/Hide/releases/tag/v1.1.0) and choose that file under **Assets**.
2. Extract the ZIP into a writable folder.
3. Double-click **Hide.exe**. A fresh installation shows the original tray icon and a settings guide.
4. Sign-in startup is enabled on first setup when registration succeeds; check the displayed status. Closing settings keeps Hide running. Double-click the program or **Open-Settings.cmd** to reopen settings; the interface is currently in Chinese.
5. Type in an editable field. Your cursor becomes 75% transparent by default. Moving about 3 screen pixels, clicking, scrolling, or stopping typing for about 1.5 seconds restores it.

Hide follows your current Windows cursor scheme. Apply a new scheme in Windows Mouse Properties and it will prepare matching transparent cursors locally. Common static CUR and animated ANI files are supported. Your original files remain intact; the portable package uses your installed skin.

Settings provide transparency, complete hiding, pause, idle restoration, sign-in startup and a tray choice. Existing preferences are preserved on upgrade. Keep the tray icon for per-app detection options. “重新识别皮肤” rescans the current skin.

**Choose quiet mode:** keep **随 Windows 登录启动** checked, then uncheck **显示托盘图标**. Hide creates a **Hide 设置** desktop shortcut and hides its tray icon; cursor fading and sign-in startup continue. Close settings. Double-click the desktop shortcut, press **Ctrl+Alt+H**, or run Hide.exe / Open-Settings.cmd to reopen settings. The **创建桌面设置快捷方式** button creates or updates this entry with the original Hide icon.

Closing settings keeps the engine running. Hiding the tray keeps it running quietly. Choosing **退出 Hide** stops this session and pauses its health check while preserving next-sign-in startup. Disabling sign-in startup prevents automatic recovery and future sign-in launches. After moving the folder, update both startup and the shortcut.

![Hide settings; uncheck the tray option for quiet mode](images/hide-settings.png)

The example shows quiet mode selected. The tray checkbox is initially checked on a fresh installation.

Backspace and Ctrl+Backspace also start fading at your selected transparency; repeated deletion keeps the cursor faded. While enabled, Hide temporarily turns off Windows “Hide pointer while typing” so partial transparency stays visible. Pause, exit and recovery restore its previous state without changing the saved Windows preference.

To start Hide at sign-in, check **随 Windows 登录启动** in settings. Hide prefers a current-user scheduled task with a 10-second logon delay, no runtime limit and no battery-related stop. Its existing guardian also supervises the engine, restoring the pointer before up to three recovery launches. A Windows health check runs once a minute to recover from supervisor loss. Normal exit pauses the health check until the next sign-in or an explicit launch. If task registration is unavailable and no task exists, Hide uses the traditional Run entry as a fallback; settings show the actual method. Uncheck to disable. Keep the folder in place; after moving it, enable startup from the new location to update the path. Hide.exe --enable-startup and --disable-startup are also available. Hide does not automatically re-enable a task you disabled in Windows.

Before fading, Hide compares the configured skin with the displayed pointer images and hotspots. If they differ, fading is paused and the visible skin is preserved. Reapply your intended scheme in Windows Mouse Properties and rescan. Restoration explicitly reloads the latest custom role files, including roles that the Windows reload call may leave stale. Hide does not automatically choose a saved named scheme.

**Ctrl+Alt+F12** immediately restores and pauses Hide. Enable it again in settings. Choose **退出 Hide** to stop the engine; closing settings keeps it running. For recovery, run **Recover-Cursor.cmd**. A separate guardian reloads your latest cursor settings after an unexpected termination. Normal exit does not trigger scheduled failure recovery.

Exit before upgrading or moving the folder. Before uninstalling, disable startup if enabled. Settings are stored in state/settings.ini. If an older version left a scheme label inconsistent with its actual cursor paths, apply your intended scheme once in Windows Mouse Properties.

Use Hide.exe --status for a fresh state snapshot; an offline engine reports running=false. Bounded state/lifecycle.log files record UTC startup, failure, recovery and exit events without typed text or key-by-key logging.

The ready-to-run file is **Hide-windows-x64.zip**. GitHub's **Source code** archives are for developers. Optional SHA256SUMS.txt is on the same release page; compare it with Get-FileHash .\Hide-windows-x64.zip -Algorithm SHA256. This build is unsigned.

Compatibility is based on tested formats and samples. Complex colored XOR, compressed bitmap formats, oversized resources and system animations with unavailable full sources may not support partial fading. Unsupported schemes keep their normal appearance; complete hiding may work. Application-drawn cursors, games, elevated windows and protected desktops have separate limitations.

Sign-in startup has about a 10-second preparation delay. If fading is absent, open settings and check the engine, skin and registration status. Long-term use, actual reboot/sleep/lock cycles, IME workflows and compatibility across applications need further testing. [v1.1.0 validation](QUIET-VALIDATION.md) · [Issues](https://github.com/DeriitoMe/Hide/issues).
