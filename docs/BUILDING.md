# 构建与测试

## 编译

安装 Visual Studio C++ 桌面构建工具和 Windows SDK，在项目目录执行：

~~~powershell
.\build.ps1
~~~

脚本使用 MSVC C++17、/O2、/MT、/W4、/guard:cf，编译 Hide.exe、输入状态测试和原生光标测试并执行它们。-NoInstall 只生成 build/ 中的程序，便于已运行实例的开发。运行时光标转换使用 C++ 和 Windows 系统图像解码器。

## 原生测试

~~~powershell
.\build\cursor_tests.exe
.\build\cursor_tests.exe --scan
~~~

默认测试使用合成素材验证格式、偏移／分配边界、10,000 次变异、ANI 元数据、PNG、黑白反色及实际背景混合。--scan 扫描本机 C:\Windows\Cursors，不修改原文件。

以下测试需要交互桌面并会短暂切换系统指针；先退出 Hide：

~~~powershell
.\build\cursor_tests.exe --desktop
python tests/desktop_validation.py
~~~

--desktop 验证 17 个系统角色的指定动画帧及顺序，结束后重载正常方案。desktop_validation.py 验证三套方案、混合角色、文件替换、损坏来源、102 次快速切换、1,000 次淡化恢复、崩溃与重启；finally 恢复测试前的注册表值。它不注入文字。后者使用本机的 Default 保存方案和已安装的 Ikaros 样本。

## 输入与恢复测试

先退出 Hide。输入集成测试显示专用 EDIT 窗口，只在它拥有输入焦点时发送字符或 Backspace：

~~~powershell
.\Hide.exe --integration-test 75
.\Hide.exe --integration-test 50
.\Hide.exe --integration-test 90
.\Hide.exe --integration-test 100
.\Hide.exe --backspace-test 75
.\Hide.exe --stress-test
.\Hide.exe --crash-test
~~~

alpha_max 集成判据用于普通 alpha 皮肤。反色皮肤应使用背景混合测试，不能用同一个 alpha_max 数值解释其透明效果。

半透明模式同时核对 GetCursorInfo 的 CURSOR_SHOWING 标志，避免仅检查指针对象而漏掉系统完全隐藏。--backspace-test 在专用窗口的固定测试文字末尾单独删除，再连续删除 8 次，核对实际可见性、透明度、删除数量及鼠标移动恢复。失去测试窗口焦点或检测到修饰键时停止注入。

结果在 state/，检查 integration-result.json 的 passed。--crash-test 直接淡化并终止自己的主程序，不发送按键。--backend-test 是非输入诊断模式；仅在该模式接受内部测试淡化／恢复消息。

## 状态与常驻测量

--status 写 state/status.json；--pause、--exit 和 --recover 分别暂停、退出及恢复。状态包含计数和资源指标，不包含按键值或文字。

cursor_showing 表示实际指针可见标志，windows_typing_hide 是 Windows 打字隐藏的运行时状态，typing_hide_lease 表示临时覆盖的恢复标记。启用且皮肤准备完成时临时关闭打字隐藏，暂停、退出或守护恢复时还原；后端诊断模式不获取此标记。

Python 桌面／测量脚本只用于开发验证。数小时观测应注明实际起止、负载、私有内存、工作集、CPU 和句柄，记录未完成与提前结束。

## 显示皮肤保护回归

先退出 Hide，运行 python tests/skin_safety_validation.py --cycles 100。它不写角色路径或方案名称，也不注入文字；核对当前 17 种角色的实际图像、前三个动画采样帧与热点。短暂覆盖内存中的箭头角色用于验证外观不一致时拒绝淡化，finally 恢复当前配置。另核对连续恢复、守护、重启与退出。句柄以连续采样及后半程趋势核对，区分异步 UI Automation 初始化与持续增长。

--enable-startup／--disable-startup 幂等设置当前用户启动项；--status 的 startup 字段核对是否指向当前可执行文件。

## 发布包

~~~powershell
python tools/package_release.py --version v1.0.0
~~~

生成 release/Hide-windows-x64.zip、SHA256SUMS.txt 和校验 JSON。明确列表打包，排除个人 state/、测试素材、证据和开发工具。历史 tools/verify_resources.py 与 resources/ 保留用于首版资源复核，不参与新版运行和打包。

新恢复标记对应内存替换：守护使用当前 Windows 配置，不保存永久绑定的角色路径。升级兼容仅在发现旧版零字节活动标记时使用旧恢复日志，正常状态不回写旧备份。
