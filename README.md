# Hide

**Fade or hide your cursor while typing.**

Hide 是一款轻量级 Windows 鼠标工具。输入文字时自动淡化或隐藏指针，移动、点击或滚动鼠标时恢复显示，并保留角色鼠标的动画。

[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4)](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0-beta.1) [![Beta](https://img.shields.io/badge/release-1.0.0--beta.1-orange)](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0-beta.1)

## 下载并使用

### [⬇ 直接下载 Windows 便携包](https://github.com/DeriitoMe/Hide/releases/download/v1.0.0-beta.1/Hide-windows-x64.zip)

[查看发布页、更新说明与 SHA256 校验文件](https://github.com/DeriitoMe/Hide/releases/tag/v1.0.0-beta.1) · [详细下载教程](docs/DOWNLOAD.md) · [English guide](docs/DOWNLOAD.en.md)

**本次是 Ikaros 验证版：需要事先安装并选中与 resources/original/ 内容匹配的 Ikaros 皮肤。相同名字但文件不同的皮肤也可能不兼容。其他指针方案会被拒绝启动。**

1. 点击上面的下载链接，保存 **Hide-windows-x64.zip**。公开下载链接可直接在浏览器中使用。
2. 右键 ZIP → **全部解压**，把里面的 Hide 文件夹放到普通可写目录。
3. 在解压后的文件夹中双击 **Hide.exe**，或 **Start-Hide.cmd / 启动.cmd**。保留同目录的 resources 文件夹和其他文件。
4. 在 Windows 托盘找到 Hide 图标，开始在文本框输入文字。默认鼠标变为 **75% 透明**。
5. 移动超过约 3 个屏幕像素、点击或滚动鼠标，指针恢复。停止输入约 1.5 秒后也会恢复。

运行不需要 Codex、GitHub 客户端、Python、.NET，也不需要为工具安装鼠标驱动。正常运行使用当前用户权限。

## 功能与设置

右键托盘图标可选择启用／暂停、50%／75%／90% 透明或完全隐藏、停止输入恢复、随 Windows 启动，以及当前应用的检测方式。开机启动默认关闭。

- 自动识别：根据前台控件类型及只读等元数据判断可编辑区域，不读取输入框文字。
- 当前应用：使用键盘检测。适用于可编辑区域识别不完整的软件。
- 当前应用：暂停淡化。适用于希望保留指针的软件。
- 当前应用：自动识别。清除该应用的例外设置。

普通字符、中文输入法组词按键及粘贴可触发；常用组合快捷键会过滤。全套中文输入法及第三方软件尚未逐一验证。

## 退出与恢复

**Ctrl+Alt+F12：立即恢复指针并暂停。** 再次启用请使用托盘菜单。

退出时右键托盘 → **退出并恢复皮肤**。遇到异常，双击 **Recover-Cursor.cmd / 恢复鼠标.cmd**，先暂停运行实例再恢复系统指针。

主程序配有恢复守护，在异常退出或持续失去响应时尝试恢复。指针阴影在淡化期间临时关闭，恢复时还原。原皮肤文件不会被修改；动画切换可能从第一帧重新开始。

工具在同目录 state/ 保存个人设置与恢复记录，常驻不记录输入文字，不逐键写日志。升级、移动目录或删除文件前，请先正常退出；主程序与守护都会退出。

## 系统与兼容性

- Windows 10／11 x64，桌面交互会话。当前端到端测试范围见下方报告。
- 已安装并选中与本版本原资源 SHA256 匹配的 Ikaros 皮肤。启动时逐文件核对。
- 软件使用 Windows 系统指针时可参与切换；自行绘制鼠标的软件、部分游戏、管理员窗口及受保护桌面可能不兼容。
- 软件识别失败时保留正常指针；当前版本不作为任意皮肤的转换工具。

如果提示皮肤不兼容，请先打开 **Windows 鼠标属性 → 指针** 检查方案。Hide 的 ZIP 不会自动安装或替换你的皮肤；其中 resources 是运行所需的透明版本与校验基准。

## 验证与资源占用

[完整验证报告](docs/VALIDATION.md) · [可复核数据](evidence/)

- Release 编译通过，/W4 无警告。
- 状态测试通过 20,044 条断言，包含 10,000 次状态转换。
- 真实系统指针的 50%、75%、90% 透明及隐藏测试通过，鼠标恢复后 alpha 回到 255。
- 原构建连续 30 次淡化／恢复通过；Hide 品牌构建另外验证首字符及崩溃恢复。
- 30 秒空闲样本：主程序与守护私有内存合计约 6.3 MiB，工作集合计约 37.2 MiB，约占单核 0.16% CPU。工作集包含共享页面；这是短时样本，不是长期或所有软件的固定占用。

当前发布为 **Beta**，数小时常驻、锁屏／休眠真实循环及跨应用兼容性仍需更多验证。发现问题请提交 [Issue](https://github.com/DeriitoMe/Hide/issues)，附 Windows 版本、皮肤、相关应用及复现步骤。

## 源码与构建

源码位于 src/，状态测试位于 tests/。安装 Visual Studio C++ 桌面构建工具与 Windows SDK，在 PowerShell 中执行：

```powershell
.\build.ps1
```

生成 Hide.exe 与 build/state_tests.exe，并运行状态测试。编译现有资源不需要 Python。只有重新准备皮肤／图标时，build.ps1 -PrepareAssets 需要 Python 与 Pillow。

下载源代码 ZIP 后，可按上述方式编译；直接运行的用户请使用本页顶部的 Windows 便携包。

[构建与测试说明](docs/BUILDING.md) · [发布规范](docs/RELEASING.md) · [更新记录](CHANGELOG.md)

Ikaros 鼠标美术是现有皮肤素材，Hide 的开发成果是输入检测、透明切换及恢复逻辑。仓库未额外授予第三方皮肤美术的使用许可。

---

Hide is a lightweight Windows utility that fades or hides your cursor while you type and restores it when you move, click, or scroll. This beta requires the exact tested Ikaros cursor files. [Download and setup in English](docs/DOWNLOAD.en.md).
