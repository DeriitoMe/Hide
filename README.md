# Hide

**Fade or hide your cursor while typing.**

Hide 是一款轻量级 Windows 鼠标工具。开始输入文字时自动淡化或隐藏指针，移动、点击或滚动鼠标时恢复。它自动跟随你当前的系统鼠标皮肤，支持静态指针和动画指针。

[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4)](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0) [![Release](https://img.shields.io/badge/release-1.0.0-blue)](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0)

## 下载并使用

### [⬇ 直接下载 Windows 便携包](https://github.com/DeriitoMe/Hide/releases/download/v1.0.0/Hide-windows-x64.zip)

[发布页与 SHA256 校验文件](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0) · [详细下载教程](docs/DOWNLOAD.md) · [English guide](docs/DOWNLOAD.en.md)

1. 下载 **Hide-windows-x64.zip**，右键 ZIP → **全部解压**，放到普通可写目录。
2. 打开 Hide 文件夹，双击 **Hide.exe**，或 **Start-Hide.cmd / 启动.cmd**。
3. 在 Windows 托盘找到 Hide。开始在可编辑区域输入文字，默认指针变为 **75% 透明**。
4. 移动超过约 3 个屏幕像素、点击或滚动鼠标，指针恢复。停止输入约 1.5 秒后也会恢复。
5. 在 **Windows 鼠标属性 → 指针** 应用另一套皮肤，Hide 会自动重新识别。准备期间指针保持正常显示。

运行不需要 Codex、GitHub 客户端、Python、.NET 或鼠标驱动。正常运行使用当前用户权限；你自己的皮肤需事先通过 Windows 安装／选择。

## 功能与设置

右键托盘可选择启用／暂停、50%／75%／90% 透明或完全隐藏、停止输入恢复、随 Windows 启动，以及当前应用的检测方式。开机启动默认关闭。

**启用开机启动**：右键 Hide 托盘图标 → 勾选 **随 Windows 启动**。以后登录 Windows 时自动启动；取消勾选即可关闭。文件夹移动后，请在新位置重新启用。也可执行 Hide.exe --enable-startup／--disable-startup，无需管理员权限。

- **自动跟随当前鼠标皮肤**：整套更换、部分角色更换、混合皮肤及原文件内容更新均会重新识别。
- **重新识别当前皮肤**：用于手动刷新。
- **当前应用：自动识别**：根据焦点控件类型与只读元数据判断可编辑区域，不读取输入框文字。
- **当前应用：使用键盘检测**：用于可编辑区域识别不完整的软件。
- **当前应用：暂停淡化**：用于希望保留指针的软件。

普通字符、中文输入法组词按键及粘贴可触发；常用组合快捷键会过滤。Backspace 等编辑键仅在已经输入时延续淡化，不单独触发。全套中文输入法和第三方软件尚未逐一验证。

## 皮肤兼容性

支持常见 Windows 系统 .cur／.ani，包含多尺寸图像、PNG 图像项、常见 DIB 和黑白反色像素。动画使用完整 ANI 来源，保留帧序、速度和热点。源文件保持原样，透明副本在本地 state/cache-v2/ 按需生成。

淡化前会核对当前配置与实际显示的指针图像和热点；不一致时暂停淡化，保留屏幕上的皮肤。请在 Windows 鼠标属性重新应用希望使用的方案后重新识别。恢复时逐一加载最新角色文件，覆盖 Windows 重载接口可能遗漏的自定义角色；不自动选择任何命名方案。

本机 223 个样本均通过转换及 Windows 加载检查；Ikaros、Default、系统默认空路径及混合方案另通过桌面切换／恢复测试。**这不代表所有皮肤、所有软件均已验证。**

复杂彩色 XOR、压缩 DIB、嵌入颜色配置文件、超过资源上限或无法取得完整来源的系统动画，可能无法半透明。此时保留当前外观，可以尝试完全隐藏。自行绘制鼠标的软件、部分游戏、管理员窗口和受保护桌面可能不适用。

## 退出与恢复

**Ctrl+Alt+F12：立即恢复并暂停。** 再次启用请使用托盘菜单。

退出时右键托盘 → **退出并恢复皮肤**。遇到异常，双击 **Recover-Cursor.cmd / 恢复鼠标.cmd**。

主程序配有独立恢复守护。淡化使用内存中的系统指针对象，恢复时装载用户当前的配置；用户在淡化期间换皮肤，退出或崩溃恢复也保留新选择。新版不修改指针阴影偏好。动画切换可能从第一帧重新开始。

state/ 保存个人设置、临时缓存及恢复标记，常驻不记录输入文字，不逐键写日志。升级、移动或删除文件前，请先正常退出。卸载前取消已启用的开机启动。

## 验证与资源占用

[完整验证报告](docs/VALIDATION.md) · [可复核数据](evidence/) · [更新记录](CHANGELOG.md)

- 原有输入状态测试通过 20,044 条断言；新增原生光标测试覆盖格式边界、动画元数据及实际背景混合效果。
- 17 个系统角色保留测试 ANI 的全部指定帧；实测避免了复制动画句柄导致的动画丢失。
- 连续 1,000 次真实系统淡化／恢复，以及 102 次快速方案切换通过；未出现持续句柄增长。
- 50%、75%、90% 与隐藏的受控首字符响应均为 47 ms；这些是本次样本。
- 1,000 次恢复测试的 40 个淡化耗时样本，P95 约 62 ms；此指标是执行淡化的时间，区别于完整输入延迟。
- 内存、CPU 的短时测量和长期验证范围见报告。真实锁屏／休眠及跨应用测试仍需扩展。

发现问题请提交 [Issue](https://github.com/DeriitoMe/Hide/issues)，附 Windows 版本、皮肤、相关应用及复现步骤。

## 源码与构建

安装 Visual Studio C++ 桌面构建工具与 Windows SDK，在 PowerShell 中执行：

~~~powershell
.\build.ps1
~~~

生成 Hide.exe 并运行输入状态与原生光标测试。程序转换皮肤使用编译好的 C++ 模块，无需 Python。开发者的桌面测试／发布脚本使用 Python；直接使用者下载顶部便携包即可。

[构建与测试说明](docs/BUILDING.md) · [发布规范](docs/RELEASING.md)

历史资源仅用于首版验证，第三方鼠标美术的权利属于原作者。新版便携包使用用户已安装的皮肤。

---

Hide is a lightweight Windows utility that fades or hides your current system cursor while you type and restores it when you move, click, or scroll. It follows cursor scheme changes and supports common CUR/ANI skins. [Download and setup in English](docs/DOWNLOAD.en.md).
