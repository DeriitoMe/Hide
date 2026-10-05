# 下载与使用 Hide

当前版本：v1.0.0，Windows 10／11 x64。

## 直接下载

1. 在浏览器点击 [下载 Hide-windows-x64.zip](https://github.com/DeriitoMe/Hide/releases/download/v1.0.0/Hide-windows-x64.zip)。
2. 右键压缩包 → **全部解压**，选择普通可写目录，例如 D:\Apps。
3. 打开 Hide 文件夹，双击 **Hide.exe**，或 **Start-Hide.cmd／启动.cmd**。
4. 在 Windows 托盘找到 Hide，必要时展开隐藏图标。
5. 在文本框输入，默认指针变为 75% 透明；移动、点击、滚动或停止输入约 1.5 秒后恢复。

不需要 Codex、GitHub 客户端、Python、.NET 或鼠标驱动。Hide 使用你已经安装的系统鼠标皮肤。解压后请保留脚本和 Hide.ico。

## 从 GitHub 页面下载

打开 [发布页](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0)，展开 **Assets**，选择 **Hide-windows-x64.zip**。

**Source code (zip)** 和 **Source code (tar.gz)** 是开发者源码；便携可执行文件在上述 Hide-windows-x64.zip 中。

## 换皮肤与设置

在 Windows 鼠标属性 → 指针中选择新方案并点击 **应用**。Hide 会自动识别新方案，准备时保持正常外观，随后输入时淡化新方案，恢复时也显示新方案。

只更换文本选择等部分角色或使用混合方案，也会重新识别。必要时右键托盘 → **重新识别当前皮肤**。

托盘菜单可选 50%、75%、90% 透明或完全隐藏、启用／暂停、空闲恢复和开机启动。开机启动默认关闭。对焦点识别不完整的软件，将其置于前台，再选 **当前应用：使用键盘检测**；**当前应用：自动识别**可清除例外。

Backspace／Ctrl+Backspace 删除也会使用当前透明度，默认 75%，连续删除保持淡化。Hide 启用期间临时关闭 Windows 的“在打字时隐藏指针”，避免系统将指针完全隐藏；暂停、退出或异常恢复时恢复此前状态，不修改永久偏好。

### 开机启动

右键 Hide 托盘图标 → 勾选 **随 Windows 启动**。以后登录当前 Windows 用户时自动启动；取消勾选即可关闭。不要在勾选后移动程序文件夹；移动后，在新位置重新启用即可更新启动路径。也可在 Hide 文件夹运行 Hide.exe --enable-startup／--disable-startup。

### 指针外观与配置不一致

如果实际显示的皮肤与配置路径不一致，Hide 会暂停淡化并保留当前可见皮肤，避免输入时换成另一套。请在 Windows 鼠标属性中重新选择希望使用的方案、点击应用，再使用 **重新识别当前皮肤**。Hide 不会替你选择其他命名方案。

## 恢复、升级与卸载

- **Ctrl+Alt+F12**：立即恢复并暂停；使用托盘菜单重新启用。
- **退出并恢复皮肤**：正常退出主程序与守护进程。
- **Recover-Cursor.cmd／恢复鼠标.cmd**：异常时恢复。
- 升级前退出旧版，再解压新版或替换程序。可保留 state/settings.ini 中的设置。
- 移动或删除文件夹前正常退出；卸载前取消已启用的开机启动。

新版自动使用当前实际生效的指针配置。如果升级前方案名称和实际角色已不一致，请在 Windows 鼠标属性里重新应用希望使用的方案一次。

## 校验与常见问题

同一发布页可下载 SHA256SUMS.txt。在下载目录的 PowerShell 中执行：

~~~powershell
Get-FileHash .\Hide-windows-x64.zip -Algorithm SHA256
~~~

输出 Hash 应与校验文件相同。本次可执行文件没有代码签名。

**某套皮肤无法淡化？** 常见 CUR／ANI 受到支持；复杂彩色反色、特殊压缩、超过限制或来源不完整的动画可能受限。Hide 会保留正常外观，可以尝试完全隐藏或换方案后重新识别。

**某个软件无法淡化？** 确认托盘已启用，再尝试该应用的键盘检测。自绘光标、某些游戏、管理员窗口与受保护桌面可能不兼容。

**要提交问题？** 在 [Issues](https://github.com/DeriitoMe/Hide/issues) 提供系统、皮肤、应用及步骤。state/errors.log 不包含输入文字；分享前检查个人信息。

[验证报告](VALIDATION.md)列出实际覆盖及尚待验证的部分。
