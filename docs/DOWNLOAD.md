# 下载与使用 Hide

当前版本：v1.0.0-beta.1，Windows x64，Ikaros 验证版。

## 先确认适用范围

Hide 独立运行于 Windows。下载和使用不需要 Codex、GitHub 客户端或开发环境。

本版本需要先安装并选中与随附 resources/original/ 一致的 Ikaros 皮肤。程序校验每个指针文件的 SHA256；仅名称为 Ikaros 并不足以保证兼容。它不会自动安装皮肤。其他皮肤的用户可以阅读源码和报告，但本次便携包会拒绝不匹配的方案。

## 方法一：直接下载

1. 在浏览器打开 [Hide Windows 下载链接](https://github.com/DeriitoMe/Hide/releases/download/v1.0.0-beta.1/Hide-windows-x64.zip)。
2. 保存 Hide-windows-x64.zip。链接来自公开 Release，浏览器可直接下载。
3. 右键压缩包 → 全部解压。选择普通可写目录，例如 D:\Apps。
4. 打开解压后的 Hide 文件夹，确认能看到 Hide.exe、resources、启动与恢复脚本。
5. 双击 Hide.exe，或 Start-Hide.cmd／启动.cmd。

请完整解压并保留文件结构；resources 的相对位置是程序启动与恢复的一部分。

## 方法二：从 GitHub 页面下载

1. 打开 [仓库](https://github.com/DeriitoMe/Hide)，找到 **Releases**；也可以直接打开 [本次发布页](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0-beta.1)。
2. 在版本说明下找到 **Assets**，必要时点击展开。
3. 下载 **Hide-windows-x64.zip**。SHA256SUMS.txt 是可选校验文件；Source code (zip)／Source code (tar.gz) 用于查看或编译源码。
4. 按方法一第 3–5 步解压并启动。

## 日常使用

程序运行后没有常驻主窗口。查看 Windows 托盘，必要时点展开隐藏图标，再右键 Hide 图标。

默认行为：开始在可编辑区域输入时 75% 透明；鼠标移动超过约 3 个屏幕像素、点击、滚动或停止输入约 1.5 秒后恢复。可调节为 50%、75%、90% 透明或完全隐藏，关闭空闲恢复，或设置随 Windows 启动。开机启动默认关闭。

对识别不完整的软件，让该软件位于前台，再从托盘选择“当前应用：使用键盘检测”。选择“当前应用：自动识别”可清除例外。“当前应用：暂停淡化”会禁用该应用的淡化。

## 停止、升级与卸载

- 临时恢复／暂停：Ctrl+Alt+F12。再次启用使用托盘菜单。
- 退出：右键托盘 → 退出并恢复皮肤。两个常驻进程均会退出。
- 异常恢复：双击 Recover-Cursor.cmd／恢复鼠标.cmd。
- 升级或移动：先正常退出，再替换或移动完整文件夹。
- 卸载：如果启用了随 Windows 启动，先从托盘取消；正常退出后删除文件夹。

## 可选：检查下载完整性

从同一发布页下载 SHA256SUMS.txt，在下载目录打开 PowerShell，执行：

```powershell
Get-FileHash .\Hide-windows-x64.zip -Algorithm SHA256
```

将输出 Hash 与 SHA256SUMS.txt 中该文件的值比较。两者应一致。

## 常见问题

**提示皮肤不兼容？**

打开 Windows 鼠标属性 → 指针，确认选中了匹配的 Ikaros 方案。相同方案名称但不同版本、缺失某个指针或改了其中一个文件，都可能导致校验失败。此时 Hide 不切换当前指针。

**某个软件输入时不淡化？**

先确认托盘处于启用状态，再尝试该应用的键盘检测设置。管理员窗口、受保护桌面、自绘指针或某些游戏的兼容性有限。

**下载后显示未知发布者？**

本次 Hide.exe 没有代码签名。可核对下载来源及 SHA256；如果设备策略阻止运行，请遵循设备管理员的要求。

**为什么只有源码，没有 Hide.exe？**

便携可执行文件位于 Release 的 Hide-windows-x64.zip。仓库的 Code → Download ZIP 下载源码，供开发者按 [构建说明](BUILDING.md) 编译。

**想提交问题？**

在 [Issues](https://github.com/DeriitoMe/Hide/issues) 提供 Windows 版本、使用的皮肤、相关软件及步骤。日志 state/errors.log 仅包含错误代码；分享截图和日志前请自行检查个人信息。
