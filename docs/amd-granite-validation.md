# Granite Ridge / 9950X3D APOB 拓扑验证

2026-11-01，继续使用 REA 6.7.0 研究 BIOS 2503 / SMU 98.84.0。GUI 与 CLI 同步新增 **APOB 拓扑记录离线解码**，保留枚举序号、存储编号、两项线程标志和重复编号。恢复了固件拓扑路径的一段读取逻辑，尚未建立 PM 到物理 CCD/OS 核心的绑定。**本轮没有新增确认的物理单位，第二 rails 写入仍未开放。**

目标为 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。目标机器不在用户手边，实际 BIOS 未知；参考固件与合成回放不替代真机验收。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41281702238) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 37 CTest、493 场景组；7 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41281705754) | 13/13；11 Linux 目标各 39 CTest、81 CLI 场景组、1580 JSON 报告；安装、卸载、重装及无 Qt/显示环境运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41282079850) | 26/26；各目标及 EL7/EL8 基线 243 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [Granite 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41281327505) | 1/1；REA 分析五个 UEFI 模块；SMU/MP5 各 12240 项编码自检；19 组本地/云端回放结果逐项一致 |
| [补充界面复核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41283566574) | 2/2；EL7 与 Ubuntu 24.04 各 243 Qt 测试，导出新界面截图；核对应用、编译和测试输入一致 |
| APOB 协议 | 两份固定哈希实现共 5348 案例：枚举 2946、序号转换 2048、错误/边界 82、排序 272；只执行选定读取函数和排序 helper |
| 解析边界 | 每个截断长度、完整 456 字节跨度、1 MiB 上限、声明长度、重复/稀疏 ID、线程前缀及逐字节变异；GUI 错误清空、CLI 零后端创建 |
| 本机扩展检查 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

**46/46 最终云端任务通过：43 个应用任务、1 个最终研究任务、2 个截图复核任务。** GUI/内核与交付快照使用 `f723de62e5a8b2c2bf1a6317d4a4726e234ecf96`；核心和 CLI 使用 `8ce0dd1727ecdaee6289135cd77470df5e032d75`；研究使用 `1e5d8ba418682a0f34fdfedfd5e1ebdee5c11fbf`。核心/CLI 通过版本之后只改了 GUI 导入文件读取及 Qt 测试，已核对它们的构建与测试输入完全相同。GUI 随后单独重跑完整发行版工作流。

研究提交之后还修正了核心测试夹具：为兼容 GCC 11/14，直接分配拼接记录和截断记录的准确长度，避免 `vector::insert/resize` 在优化编译时触发边界诊断。没有关闭编译告警或跳过测试。被替换的应用批次不计为通过。

REA 首轮未自动识别运行时写入函数表的 `0x1840` 入口，随后使用 REA 自带的分析种子支持，明确记录每份模块的 12 个分析者指定入口，并核对反编译结果。此前失败和种子确认批次不计入最终 46 项。GUI 新增测试还修正了漏写的类型命名空间。本机首轮测试受默认临时目录权限限制，换用项目内临时目录后通过。迭代原因和运行编号保留于验证 JSON。

截图导出清单漏了新截图，后续提交 `0f6244c776a0b098ba7ee9a4194da2c5235aa085` 只增加两条 PNG 复制语句并调整补充复核工作流；两个代表环境重新运行相同测试并导出截图，未重复全部发行版矩阵。原矩阵留存 1617 张，补充留存 2 张。

最终 main 文档提交不另算一次应用测试。应用和编译输入与对应通过版本一致；截图导出脚本单独记录补充验证哈希，26 个研究源文件与 REA 通过版本一致。此前 18 组本地回放的脚本未改变，已与本次云端结果重比；新增第 19 组在本机和云端均执行并逐项比对。详见[验证 JSON](validation/amd-granite-ci-f723de6.json)和[研究 JSON](validation/amd-granite-research.json)。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。EL7 指 Oracle Linux 7.9；Windows/macOS 只验证共享核心。生成 33 个安装包、1619 张留存截图；直接检查 EL7 与 Ubuntu 24.04 的新 APOB 记录视图。截图来自合成记录。QEMU 验证驱动集成，未验证目标机调参或 Secure Boot。

CLI 用法：`amd-topology-decode --file topology-record.bin`。GUI 在 AMD tuning 选择 Open firmware topology record。两者均要求带 16 字节头的单条 APOB group 3/type 3 记录，记录长度至少 456 字节且与声明相同；不接受完整 BIOS 或 PM 表，不自动读取物理内存。匹配格式不能认证实际 BIOS 身份。

REA 追到 APOB 记录选择和 `D20482F8` 协议：记录里的编号与枚举序号不同，线程数按非零前缀计数；排序 helper 返回按存储编号排序后的原枚举序号。协议 `+0x48` 实际含寄存器读写，并非反向映射函数，未执行、未开放。详见[原始布局、哈希与限制](amd-apob-topology.md)。

剩余八行仍是核心 **0/4/12**、CCD **0/1/2/6/8**；还缺模型标定、计数事件身份及独立时间基准。目标机采用哪份拓扑实现、记录生产路径和物理身份、PM 槽位到 CCD/OS 核心绑定、第二 rails 独立写入与设备回读均未完成。既有 Fmax 读写与保存值回读保留。

交付目录 `dist/gui-cli-amd-granite-f723de6/` 含 33 个安装包及源码。根目录源码包保留应用云端测试快照；推荐 `updated-source/`，含最终文档及八项既有用户扩展。用户扩展仅作本机可执行检查，未冒充与云端源码完全一致。`SHA256SUMS` 覆盖全部交付文件，研究依赖不加入 GUI/CLI 安装包。
