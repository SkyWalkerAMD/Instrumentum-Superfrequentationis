# AMD PM 链路与片内时钟监控整批验证

2026-10-20：生产提交 `c3631f4d2b987b36b4af47ab6ab5d43804c2ddc9`，GUI / CLI 同步恢复两组共 27 项原版监控存储值。
加上已有 24 项公共遥测、24 项原版限制及 53 项公共功耗/策略监控，共 128 个显示字段。
[实现范围](amd-pm-fabric.md)、[完整验证记录](validation/amd-pm-fabric-ci-c3631f4.json)。

| 检查 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39420863989) | 4/4；各 25 CTest / 367 场景组，含 34 组 PM 核心回归 |
| ASan / UBSan | 五个离线解析目标通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39420871784) | 13/13；11 目标各 27 CTest / 50 CLI 场景组 / 1132 JSON，PM 报告共 72 份 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39420879909) | 26/26；各目标及 EL7/EL8 基线 181 项 Qt 回归，107 分析测试 / 33 移植测试 |
| 原指令映射 | 128 组合成上下文，各 27 字段；云端与本机完整报告内容和 SHA256 一致 |
| 内核 QEMU | 12 次匹配内核启动；装卸、CPUID、能力、非法 CPU、非特权拒绝通过 |
| 本机用户源码 | 49 项：37 通过、12 缺 Linux/C++ 工具链而跳过；相应云端检查完整运行 |
| 真机 / Secure Boot | 未执行 |

43/43 功能验证任务绑定同一提交。测试覆盖八个精确版本、每个新字段的逐位输入、
NaN/Inf/负零、错误隔离、BCLK 不影响新字段、旧快照兼容、伪造显示值重算、
元数据变化及失败后清空。前两次云端运行发现 CLI 夹具退出码预期错误、报告总数断言遗漏及 ASan 截断测试超时，修复测试并保留完整覆盖后重新整批验证。GUI 测试覆盖两组切换、导入/导出和后台读取错误。

保留 1100 张截图；人工检查 Ubuntu 24.04 与 Oracle Linux 7.9 的链路组、
片内时钟组和错误页面各三张，共六张；其余 1094 张未逐一人工检查。
链路页面为 1150×650，时钟及错误页面为 1150×940；数值均为合成数据。

`dist/gui-cli-amd-pm-fabric-c3631f4/` 包含 33 个 GUI/CLI/DKMS 安装包、
精确云端源码和保留八项本地扩展的 `updated-source/` 源码、MANIFEST、SHA256SUMS。
276 个生产/构建源码路径逐一核对。目标为 Ubuntu 20.04/22.04/24.04/26.04、
Debian 11/12/13、Oracle Linux 7.9、Rocky Linux 8/9/10；EL7 指 Oracle Linux 7.9 x86_64。
Windows/macOS 仅验证独立核心。包版本仍为 2.0.1，本地更新需使用发行版重装方式。

REA/Ghidra 研究另见 [辅助分析后续记录](amd-pm-rea-followup.md)，不合并进 43 项功能验证任务。
GMI、DIMM、PCIe 的 [92 个数组存储字研究](amd-pm-array-research.md)尚未接入程序，不计入新增功能。
新增监控的物理单位、实际策略状态、物理目标尚未认证；逐核/CCD PM、原生传输、
AMD 高层调参、其它 Intel 协议与板级控制仍见[剩余清单](recovery-status.md)。
