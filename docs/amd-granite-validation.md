# Granite Ridge / 9950X3D 配置通道与拓扑验证

2026-11-01，继续使用 REA 6.7.0 研究 BIOS 2503 / SMU 98.84.0。GUI 与 CLI 同步补充 CCD 第 2 行的配置选择语义，纠正把两个输入当成两个可独立寻址 rails 的解释。**本轮没有新增确认的物理单位，也没有开放第二 rails 写入。**

目标为 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。目标机器不在用户手边，实际 BIOS 未知；参考固件与合成回放不替代真机验收。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41276893928) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 36 CTest、488 场景组；6 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41276899123) | 13/13；11 Linux 目标各 38 CTest、80 CLI 场景组、1572 JSON 报告；安装、卸载、重装及无 Qt/显示环境运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41276903816) | 26/26；各目标及 EL7/EL8 基线 241 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [Granite 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41277923617) | 1/1；REA 分析两个 UEFI 传输模块和一个拓扑模块；SMU/MP5 各 12240 项编码自检；18 组本地/云端回放结果逐项一致 |
| 配置通道 | 1344 组广播，756 组非成功但非零的确认值仍获主机成功返回；2 个停滞目标；378 组成对目标选择、756 次值选择和 756 次旧值保留 |
| 模型消费路径 | 128 组成对目标、3072 次反馈选择；区分模型复合值与相邻反馈流，未据此赋予瓦特等单位 |
| UEFI APIC 构造 | 256 组合成 CPUID 回放；REA 复核构造函数、CPUID 包装、入口和三个调用者；调用者本身未执行 |
| 本机扩展检查 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

**44/44 最终云端任务通过：43 个应用任务、1 个最终研究任务。** 应用、核心、GUI/内核工作流均使用 `97759e849bf64aad01b9b072846e2d4d2af5d044`；最终研究工作流使用 `22f2a51c219dc18e3ed30d8ee8bf44ac34ef5546`。两次先前成功的 REA 研究迭代不计入这 44 项。中间研究提交 `25749d3` 的派发未返回运行编号，修正依赖安装步骤的 YAML 块后重新派发；最终研究运行成功。

应用通过提交之后只增加研究脚本、研究工作流与文档。25 个研究源文件与最终 REA 通过版本逐项相同；应用和构建源码与应用通过版本相同。最终 main 文档提交不另算一次应用测试。验证提交、产物摘要、源码比较与限制见[验证 JSON](validation/amd-granite-ci-97759e8.json)和[研究 JSON](validation/amd-granite-research.json)。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。EL7 指 Oracle Linux 7.9；Windows/macOS 只验证共享核心。生成 33 个安装包、1617 张截图；直接检查 EL7 与 Ubuntu 24.04 的 Granite PM 视图。GUI 截图来自合成表数据。QEMU 验证驱动集成，未验证目标机寄存器调参或 Secure Boot。

GUI 的 CCD 第 2 行标为“Selected configuration; unit unverified”，CLI/JSON 提供相同的 `configuration_protocol` 元数据；导入会从原始表重新生成它，不信任伪造的 rail、回读或写入可用性声明。已有 [Fmax 读取、设置与保存值回读](amd-granite-fmax.md)继续保留。

剩余八行仍是核心 **0/4/12**、CCD **0/1/2/6/8**。模型缺少系数标定，计数器缺少事件身份与独立时间基准。固件协议枚举坐标尚未绑定到 PM 槽位和物理 CCD/OS 核心；第二 rails 仍缺少物理身份、独立写入和设备侧回读的完整证据。详见[本轮配置与拓扑研究](amd-granite-config-routing.md)。

交付目录 `dist/gui-cli-amd-granite-97759e8/` 含 33 个安装包及源码。根目录源码包保持应用云端测试时的原始快照；推荐 `updated-source/` 源码包，含随后通过验证的研究脚本、最终文档及保留的八项用户扩展。清单明确区分这两种来源，`SHA256SUMS` 覆盖全部交付文件。研究回放依赖仅供研究环境使用，不加入 GUI/CLI 安装包。
