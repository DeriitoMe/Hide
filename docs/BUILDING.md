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

--status 请求实时 state/status.json；未运行时返回 running=false、退出码 2。--pause、--exit 和 --recover 分别暂停、退出及恢复。状态包含计数和资源指标，不包含按键值或文字。

cursor_showing 表示实际指针可见标志，windows_typing_hide 是 Windows 打字隐藏的运行时状态，typing_hide_lease 表示临时覆盖的恢复标记。启用且皮肤准备完成时临时关闭打字隐藏，暂停、退出或守护恢复时还原；后端诊断模式不获取此标记。

Python 桌面／测量脚本只用于开发验证。数小时观测应注明实际起止、负载、私有内存、工作集、CPU 和句柄，记录未完成与提前结束。

## 显示皮肤保护回归

先退出 Hide，运行 python tests/skin_safety_validation.py --cycles 100。它不写角色路径或方案名称，也不注入文字；核对当前 17 种角色的实际图像、前三个动画采样帧与热点。短暂覆盖内存中的箭头角色用于验证外观不一致时拒绝淡化，finally 恢复当前配置。另核对连续恢复、守护、重启与退出。句柄以连续采样及后半程趋势核对，区分异步 UI Automation 初始化与持续增长。

--enable-startup／--disable-startup 幂等设置当前用户启动项；--status 的 startup 字段核对是否指向当前可执行文件。

## 静默、设置与任务启动回归

双击程序或 --settings 打开现有实例的原生设置窗口。--autostart 静默启动且不打开设置；--quiet 同时关闭此次运行的托盘图标。--hide-tray／--show-tray 可设置并更新已有实例的图标显示。设置关闭后释放窗口、控件和字体，输入引擎继续运行。外部 Hide.ico 缺失时使用 exe 内置的原图标。

v1.1.0 新安装默认显示托盘、首次使用引导及登录自启。设置文件已存在时保留原偏好；SetupComplete 标记避免重复首次设置或覆盖已禁用任务。--create-shortcut 使用当前用户桌面建立原图标入口，目标为当前 exe --settings，键盘入口 Ctrl+Alt+H。图标选择、静默、关闭设置和退出的语义见下载教程。

python tests/onboarding_validation.py 在干净 build/state 设置下验证默认托盘、自启、可见引导、静默选择、桌面链接及再次任务启动；结束恢复开发设置及原桌面链接。应先退出实例并在隔离账号移除测试任务，脚本拒绝替换已有任务。

输入监听初始化最多尝试 3 次。正常启动由 --supervise 运行现有恢复守护，并通过 --engine 启动输入引擎，总共两个原生进程。监督进程先恢复指针及系统偏好，再最多重启异常引擎 3 次，稳定运行 5 分钟后重置短时重试预算。Windows 登录任务包含每分钟 HealthCheck 时间触发器，补上监督进程也退出的情况；正常退出关闭此触发器并保留登录触发器，明确启动或下次登录后恢复。已禁用的整项任务不会被重启。恢复失败或重试预算耗尽时暂停健康检查并保留诊断。

状态增加 hooks_ready、guardian_alive、guardian_pid、supervised、show_tray、tray_added、settings_open、sample_utc、startup_backend、startup_registered、startup_path_matches 和 startup_last_result。登记状态和当前健康状态分开核对。实际验证发现 Windows 的失败重试设置本身没有在终止引擎后生效，因此以监督进程和健康检查的实测结果为恢复依据。

先退出 Hide，执行 python tests/quiet_startup_validation.py。脚本使用 build/Hide.exe，临时登记当前用户的登录任务，结束后恢复原 Run 项及开发目录设置。为保护已安装的任务，若已存在 Hide 任务则拒绝运行，应使用隔离测试账号。它验证最低权限交互登录、10 秒延迟、每分钟健康检查、取消电池及运行时长限制、静默运行、25 次设置开关、手动禁用、引擎重启、监督进程丢失恢复和正常退出暂停检查；最后调用原生 Backspace 受控测试。

开发测量可运行 python tools/resident_observer.py --hours 8 --interval 60。它只收集本机合计内存、CPU、句柄及事件数量，不注入按键、不重新启动程序；主程序退出、重启或失去监听时明确报告 interrupted。辅助 Python 观测进程不参与产品常驻，也不随 ZIP 分发。

state/lifecycle.log 使用 UTC 日期时间、版本及 PID，记录启动阶段、故障、恢复和退出。约 128 KiB 时轮转，保留一个旧文件；相同错误按分钟限流，不逐键写日志。

## 发布包

~~~powershell
python tools/package_release.py --version v1.1.0
~~~

生成 release/Hide-windows-x64.zip、SHA256SUMS.txt 和校验 JSON。明确列表打包，排除个人 state/、测试素材、证据和开发工具。历史 tools/verify_resources.py 与 resources/ 保留用于首版资源复核，不参与新版运行和打包。

新恢复标记对应内存替换：守护使用当前 Windows 配置，不保存永久绑定的角色路径。升级兼容仅在发现旧版零字节活动标记时使用旧恢复日志，正常状态不回写旧备份。
