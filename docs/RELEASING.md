# 发布规范

1. 核对版本、皮肤支持范围及实际验证覆盖。尚未完成长期或跨应用验证时使用 GitHub Prerelease。
2. 更新 README、下载教程、CHANGELOG 和验证报告。产品使用说明应能让普通 Windows 用户独立下载和运行。
3. 执行 build.ps1 和 tools/verify_resources.py。检查 GitHub Windows CI；桌面端到端测试在交互会话中单独进行。
4. 生成便携包并检查 ZIP 内文件结构、exe 一致性及 SHA256。不上传个人设置、恢复备份、输入数据、凭据或工作区临时文件。
5. 发布 Release 资产 Hide-windows-x64.zip 和 SHA256SUMS.txt。发布说明写明支持范围、使用方法、已知限制和本次测试。
6. README 顶部提供下载链接。预发布版使用固定标签下载地址；稳定版可采用 /releases/latest/download/Hide-windows-x64.zip。后续保持资产文件名一致。
7. 从公开下载地址重新下载，确认 ZIP SHA256 与发布前一致，并复核源码提交、标签和资产。

GitHub 的链接规则见 [官方文档](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases)。项目当前没有自动发布工作流；Windows 工作流只构建与测试。
