# Shared power adjustment：整批验证

GUI 与 CLI 的 A8 读取、A7 写入和写后读回，在同一提交 `f479d1d59d521eee68ff434a203a9f7db5974403` 验证。
严格限制 SMU 115.46.0，保留有符号原始字、完整能力比较、异常结果和未测量状态。

| 验证 | 结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40297009057) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 34 CTest、453 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40297009003) | 13/13；11 Linux 目标各 36 CTest、72 CLI 场景组、1504 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40297008915) | 26/26；各目标及 EL7/EL8 baseline 各 229 Qt 测试，142 分析测试、34 移植测试 |
| [REA 原版程序](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40295599470) | 1/1；3 个精确函数，文件偏移与 PIE 映射交叉校验 |
| [SMU / MP5 / REA UEFI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40297008975) | 1/1；SMU 1385、MP5 637 个函数；两个 REA UEFI 入口；1 个版本查询、256 个有符号读取、1572 个调节案例；既有 72 个复制及 20160 个 rails 案例仍通过 |
| [截图补充验收](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40300351893) | 2/2；EL7、Ubuntu 24.04 再次编译同一 GUI 并各通过 229 Qt 测试，保存新增成功/失败截图 |
| 安装与内核 | 11 目标 GUI 显示启动；CLI 无 Qt/显示/编译器安装、卸载、重装及非 root diagnose；12 次匹配内核 QEMU 启动 |
| 本机用户源码 | 50 项 port 测试：37 通过，13 因 Windows 环境跳过 |

主矩阵应用测试已通过，但截图复制清单遗漏两张新图。补充提交只添加四条复制命令和对应证据工作流；核对应用输入完全相同后，对 EL7、Ubuntu 24.04 单独补跑界面验收。合计 **47/47** 云端任务。核心包含所有 256 种规范符号扩展字的解码检查；
读接口逐访问点故障注入、取消、固件版本拒绝、身份变化；写接口检查完整能力变化、
旧值冲突、未变化值、固件拒绝、确认后未存储，以及写入后版本/能力/访问失败。
GUI 额外验证读取前禁止应用、确认取消、确认期间输入变化、异步输入变化、过期快照和失败显示。
CLI 在打开后端前拒绝非法参数，并由独立进程解析 JSON，核对负数、null 和验证标志。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 为 Oracle Linux 7.9；Windows/macOS 验证可移植核心，不表示完整应用移植。
既有五项离线解析 ASan/UBSan 验证通过。

294 个运行/构建文件与成功提交逐一核对；其中两个证据收集文件来自补充 GUI 验收提交 `37b1aa95111526dc54fdc0cdbf3012784b0ac7a6`，应用代码与原矩阵相同。9 个研究文件匹配对应成功分析提交。
用户工作目录保留八项既有扩展及 UMC 文档别名。交付包含 33 个 GUI/CLI/DKMS 包及源码，
主矩阵每目标保留 131 张截图，共 1441 张；补充验收新增四张图，共 1445 张。人工查看 EL7、Ubuntu 24.04 的调节成功/失败四张图。

目录：`dist/gui-cli-amd-power-adjustment-f479d1d/`。根源码包为精确云端验证源，
`updated-source/` 包含本次文档、验证记录和保留的本地扩展。版本号仍为 2.0.1。
包和源码摘要见该目录 `SHA256SUMS`；[完整验证记录](validation/amd-power-adjustment-ci-f479d1d.json)，
[研究记录](validation/amd-power-adjustment-research.json)，[使用与协议](amd-power-adjustment.md)。

本轮闭合了 A7/A8 的输入、存储和读回，未进行真实硬件调参、稳定性、功耗效果或 Secure Boot 验收。
计数的物理单位未确定；逐核/CCD 字段标签、单位、有效掩码和物理映射，
第二 rails 消费与读回，以及其它尚未恢复的平台功能仍未完成。`61h` 运行时写入继续拒绝。
