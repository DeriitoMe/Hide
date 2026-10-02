# 构建与测试

## 编译现有源码

安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载和 Windows SDK。在项目目录运行 PowerShell：

```powershell
.\build.ps1
```

脚本使用 MSVC C++17、/O2、/MT、/W4、/guard:cf，编译资源与 manifest、主程序和状态测试，并自动运行状态测试。生成 Hide.exe。运行包必须保留 resources/ 和 Hide.ico。

GitHub Actions 的 Windows 工作流运行同一构建和状态测试，并检查光标资源与便携包完整性。它不在非交互会话中伪装执行桌面端到端测试。

## 光标资源校验

资源转换工具使用 Python 和 Pillow；编译现有资源不需要它们。资源验证只依赖 Python 标准库：

```powershell
python tools/verify_resources.py
python tools/verify_resources.py --source C:\Windows\Cursors\Ikaros
```

第一个命令核对随附原文件、四套透明版本的 alpha、ANI 帧序／速度／热点及 XOR 情况。第二个命令额外比对本机安装的源文件，确认它们没有被修改。重新转换指定皮肤时使用 build.ps1 -PrepareAssets（默认源目录为 C:\Windows\Cursors\Ikaros）。这不扩展产品对任意皮肤的支持范围。

## 桌面端到端测试

先退出已运行的 Hide。测试需要当前桌面选中匹配的 Ikaros 方案，会短暂显示自己的输入窗口并移动鼠标。关闭或切换前台窗口可能中止测试。输入测试只在自己的窗口与输入框保持焦点时发送字符；不会向其他程序输入。

```powershell
.\Hide.exe --integration-test
.\Hide.exe --integration-test 50
.\Hide.exe --integration-test 90
.\Hide.exe --integration-test 100
.\Hide.exe --stress-test
.\Hide.exe --crash-test
```

- integration-test：真实首字符、实际系统指针 alpha 和鼠标恢复。
- stress-test：连续 30 次受控字符输入／鼠标恢复。
- crash-test：独立激活真实系统指针淡化，核对 alpha 后终止自己的主进程（退出码 99），验证守护恢复；此模式不发送按键。

结果写在 state/。每次集成测试后检查 integration-result.json 的 passed，不只看进程退出码。焦点诊断仅测试模式写 integration-focus.json，内容是控件元数据和状态，不包含文字。

## 状态与恢复命令

Hide.exe --status 将实时计数和资源指标写到 state/status.json。--pause 暂停并恢复，--exit 正常退出，--recover 先暂停当前实例再重新装载正常方案。桌面 UI 程序从 PowerShell 启动可能异步返回，脚本测量时请等待辅助进程退出再读取状态文件。

## 生成发布包

```powershell
python tools/package_release.py
```

生成 release/Hide-windows-x64.zip、SHA256SUMS.txt 和 package-verification.json。ZIP 使用明确的文件列表，不打包 state/、.git/、源码编译中间文件或旧发布包。
