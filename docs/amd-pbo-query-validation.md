# Shimada 固件状态查询：整批验证

GUI 与 CLI 共用 LN2 / 逐核性能原始字查询，应用实现提交 `5f41602bc441ad7b7f945331b47c7eb7537cc1ef`。
核心、CLI 验证在该提交完成；GUI/Linux 验证提交为 `e7cb847499358322a20a92090ffa5ec01e0a2f6e`。
后者仅修正一个 GUI 测试的十六进制大小写期望，另新增原指令研究；应用实现和核心/CLI 构建输入不变。

| 验证 | 结果 |
|---|---|
| [跨平台核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40259016471) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 33 CTest、443 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40259021257) | 13/13；11 个 Linux 目标各 35 CTest、70 CLI 场景组、1465 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40259801723) | 26/26；各目标及 EL7/EL8 baseline 各 224 Qt 测试；142 分析测试、34 移植测试 |
| 安装与运行 | CLI 无显示、无编译器、无 Qt 安装/重装与非 root diagnose；GUI 显示启动 |
| 内核 | 12 次匹配内核 QEMU 启动，模块加载/卸载、CPUID、权限和能力检查 |
| 本机用户源码 | 50 项 port 测试：37 通过、13 因 Windows 环境跳过 |
| [REA 研究](amd-pbo-query-research.md) | 10 函数、848 次原指令实验、5 项新分析回归；也包含在 Linux 142 项测试中 |

成功矩阵合计 **43/43**，REA 单独记录。ASan/UBSan 继续覆盖既有五个离线解析测试。
目标为 `el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`，均为 x86_64。
EL7 是 Oracle Linux 7.9；Windows/macOS 仅验证可移植核心，不代表完整应用移植。

首次 Linux 任务 [40259026180](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40259026180)
两个 baseline 都成功编译，Qt 各 223 通过、1 失败：测试写成 `0xdeadbeef`，界面遵循现有 `0xDEADBEEF` 格式。
停止其余工作后仅修正断言，新任务完整重跑通过；未将失败任务计入通过数量。

293 个运行/构建路径与成功 Linux 源码包、提交逐一比对；相对核心/CLI 提交只差上述测试字面量。
用户源码保留八项既有扩展与历史 UMC 文档别名。安装包分别对应机器记录中的精确提交，
所有 33 个 GUI/CLI/DKMS 包及源码都附 SHA256。11 目标各保留 129 张图，共 1419 张；
人工检查 EL7 和 Ubuntu 24.04 的 LN2、性能原始字、未知状态六张截图，确认关键状态可见。

交付目录 `dist/gui-cli-amd-pbo-query-5f41602/`：根源码包对应 Linux 验证提交，
`updated-source/` 包括新文档和保留的用户扩展。包版本仍为 2.0.1，同版本更新使用包管理器重装。
完整记录见 [JSON](validation/amd-pbo-query-ci-5f41602.json)。

性能字保持不透明，不解释为功耗、排名或物理 CPU；索引范围是应用限制。
LN2 未知字报格式错误，不显示为关闭；此接口不访问主板 EC，也不启用 LN2。
逐核/CCD PM 真实布局、双 rails 写入、动态 EC 控制和其余未还原项目仍未完成。
没有进行真机调参、遥测精度、稳定性或 Secure Boot 验收。
