# 固件布局与 OC 状态修正：整批验证

GUI / CLI 的逐核和 CCD 存储数组、OC 模式位修正、扩大快照导入均在同一提交 `472e7855e888def37016a59039a95cae2fb41060` 验证。
研究提交 `64a2b00d4986b883c49d248bafc98775165b85cd` 只追加 rails 原指令回放脚本和独立研究工作流步骤，运行时代码不变。

| 验证 | 结果 |
|---|---|
| [跨平台核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40287709365) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 33 CTest、446 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40287709367) | 13/13；11 个 Linux 目标各 35 CTest、70 CLI 场景组、1465 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40287709361) | 26/26；各目标及 EL7/EL8 baseline 各 225 Qt 测试；142 分析测试、34 移植测试 |
| 安装与运行 | CLI 无显示、无编译器、无 Qt 安装/重装与非 root diagnose；GUI 显示启动 |
| 内核 | 12 次匹配内核 QEMU 启动，模块加载/卸载、CPUID、权限和能力检查 |
| 本机用户源码 | 50 项 port 测试：37 通过、13 因 Windows 环境跳过 |
| [REA / 固件](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40288469478) | 1/1；SMU 280、MP5 637 个函数，两个 REA UEFI 入口；72 个数组复制、20160 个 rails 处理案例 |

主矩阵 **43/43**，独立研究任务 **1/1**。ASan/UBSan 覆盖既有五个离线解析测试。
目标为 `el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`，均为 x86_64。
EL7 是 Oracle Linux 7.9；Windows/macOS 仅验证可移植核心，不代表完整应用移植。

新增数组覆盖全部八个精确版本，18 行 core、13 行 CCD；验证逐字覆盖、最后偏移、异常字隔离、
伪造派生字段重算、旧 schema-1 导入，以及 1 MiB 边界。GUI / CLI 使用同一解码器。
OC 测试覆盖能力 bit 0 与运行状态 bit 16 的区别、前置条件、完整能力变化与回读不一致。

早期失败任务完整保留在机器记录：新增 OC 状态行后的两个旧断言已修正；数组批次发现共享文件
读取器仍限制 128 KiB、三个旧视图数量断言及 MSVC 同名变量诊断。修复后重跑完整矩阵，
没有把失败任务计入通过数量。当前读取器保留 4 KiB 默认值，其它格式保留自己的调用上限。

293 个运行/构建路径与成功源码包、提交逐一比对；独立固件研究工作流与其成功提交另核对。
用户源码保留八项既有扩展及 UMC 文档别名。33 个 GUI/CLI/DKMS 包及源码均附 SHA256。
11 目标各 131 张图，共 1441 张；检查 EL7、Ubuntu 24.04 的 core、CCD 和 OC 模式六张图，
确认槽号、Unverified 单位、原始字/偏移、状态文字，以及 `7 → 10007h` 的模式回读可见。

交付目录 `dist/gui-cli-amd-firmware-pm-472e785/`：根源码包对应应用验证提交，
`updated-source/` 包含独立研究、验证文档和保留的用户扩展。包版本为 2.0.1，同版本更新需重装。
完整记录见 [JSON](validation/amd-firmware-pm-ci-472e785.json)。

存储布局已核实；字段名称、单位、有效核心掩码和物理映射仍未验收。
双 rails 的编码、OC 前置条件和状态槽钳制已核实，第二物理轨及消费/回读链未闭合，`61h` 写入仍拒绝。
没有进行真实硬件调参、遥测精度、稳定性或 Secure Boot 验收。
