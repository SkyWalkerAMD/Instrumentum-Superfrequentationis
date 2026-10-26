# Shimada 全核曲线：整批验证

应用源码提交 `40d723a1bc24c3789b53381ed9f6a2e4c7c444d2`，GUI 与 CLI 同步新增 `amd-curve-all-set` 对应的全核 CO 设置。
只开放 Shimada `1Ah/08h`、PCI `1022:153a`；完整能力字比较后发送 RSMU `07`，
整数计数范围 `-50..50`。返回 1 只代表固件确认，没有全核回读或真实硬件效果验证。

| 验证 | 结果 |
|---|---|
| [跨平台核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40253507421) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 32 CTest、436 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40253507435) | 13/13；11 个 Linux 目标各 34 CTest、68 CLI 场景组、1421 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40253507439) | 26/26；各目标及 EL7/EL8 baseline 各 220 Qt 测试；134 分析测试、34 移植测试 |
| [补充截图检查](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40255330198) | 2/2；EL7、Ubuntu 24.04 各 220 Qt 测试及全核曲线成功/失败截图 |
| 安装与运行 | 11 目标 CLI 在无显示、无编译器、无 Qt 环境安装/重装及非 root diagnose；GUI 显示启动验证 |
| 内核 | 12 次匹配内核 QEMU 启动，模块加载/卸载、CPUID、权限和能力检查 |
| 本机用户源码 | 50 项 port 测试：37 通过、13 因 Windows 环境跳过 |
| [REA 与原指令研究](amd-curve-all-research.md) | 8 函数、120 次原指令实验、3 项新增分析回归通过；另有 8 个 PM 长度/布局实验 |

主要验证 43/43，补充视觉验证 2/2，合计 45/45。REA 单独执行，不混入测试任务数量。
11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 是 Oracle Linux 7.9；Windows/macOS 只验证可移植核心，不表示完整应用已移植。
ASan/UBSan 继续覆盖已有五个离线解析测试；QEMU 没有验证实际寄存器设置或 Secure Boot。

补充任务提交 `a8dd903f15999b8183e0f12185d3ac628daed889` 修复截图保留：主矩阵生成了两张新图，
但容器脚本未把它们复制到上传目录。修改仅为四条复制命令与独立证据 workflow。
292 个运行/构建路径已核对，290 个保持原样，上述两个证据脚本单独记录；
补充任务还独立确认应用、测试和其它构建输入未改变。33 个安装包仍来自精确应用提交 `40d723a`。
主矩阵保留 1364 张截图，补充任务保留 4 张；人工检查了后四张，验证成功确认、失败不确定状态及不可回读提示可见。

本机完整分析套件运行 137 项：133 通过、3 项 Linux 编译器检查跳过，
1 个未修改的 Intel TVB busy-loop 回放触发模拟器 1 秒保护而报错。
不改源码立即单独复跑 TVB 两项，全部通过；三项新 CO 回归在完整套件中均已通过。
初始失败与定向复跑日志均保留，未把首次完整运行写成全部通过。
云端应用提交的 134 项分析测试均通过；后续三项 REA 回归在本机运行，不声称计入该云端总数。

交付：`dist/gui-cli-amd-curve-all-40d723a/`，含 33 个 GUI/CLI/DKMS 包、精确云端源码、
`updated-source/` 最新用户源码、验证记录与 SHA256 清单。用户源码保留八项既有扩展和历史 UMC 文档别名。
包版本仍为 2.0.1，同版本更新使用对应包管理器的重装操作。

逐核/CCD 真实 PM 布局和双 rails 写入仍未完成。长度关系不能确定 CCD 拓扑或字段偏移，
原 Force/UnForce 也不能证明 CO/电压复原协议。AMD PStates 写入、其它 AMD 平台及 Curve Shaper、
部分 Intel server/主板控制、动态内存/EC 和真机验收继续保留为未完成项。
