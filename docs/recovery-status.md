# 功能还原状态

<!-- headless-cli-b2ef0a3 -->
2026-10-10 无界面入口已增加：`octool-cli`，验证源码 `b2ef0a3`。
它复用本页已有的核心功能边界，提供 SSH/终端操作和 JSON 输出，不新增未还原的硬件语义。
主板/传感器/SPD 读取移入 GUI、CLI 共用的无 Qt 实现。
[使用说明](headless-cli.md)、[验证记录](headless-cli-validation.md)：十个发行版各 9 CTest，独立 CLI 安装/运行/卸载重装通过，既有 Linux 23/23 回归通过。
PStates 写入、曲线写入、Intel server VF/fabric、板级专用控制等缺口及四台真机验收仍保留。

以下保留此前记录。

当前验证生产代码为 `06b477c`，新增 [Shimada 原始曲线查询](amd-curve-query-recovery.md)，并保留此前 Intel offset / 最大 OC 倍频、UMC 和拓扑功能。
[本轮验证](validation/amd-curve-recovery-ci-06b477c.json)为核心 4/4、Linux 23/23；安装包与原目录源码均已同步。
原版全部功能仍未完成。安装兼容、自动化回归、原指令研究和真实硬件验收分别记录，
没有能代表四大模块全部功能的可靠百分比；212 个 UMC 字段也不是整个软件的功能分母。

| 用户要求的模块 | 已接入可编译程序 | 尚未还原的部分 |
|---|---|---|
| AMD PStates | 按逻辑 CPU 采样 P0–P7、上限/能力检查、频率、64 位原值、完整 9 位 VID / Idd 编码 | 型号专用 mV/A 换算；满足跨核/跨 coherent fabric 一致性的设置 |
| Intel Controls | RAPL 功耗/时间窗、HWP、温度；Raptor Lake-S 单域 core/cache offset 与最大 OC ratio、保留其它字段、锁/旧值检查及回读 | W790/W890 电压域；VF 点、逐核 turbo/ratio/VID rank/SP、fabric/BCLK |
| AMD 调参 | 三套独立 BIOS SMUIO；Shimada 已提取命令和逐核频率参数准备；CPUID 稀疏拓扑；限定身份的原始曲线查询 | CPUID 与固件目标 ID 对应；MP1/PBO 高层单位、曲线设置/电流/温度限制、PM 表、profiles/hotkeys |
| 内存与主板 | DMI/BIOS、内核传感器、绑定驱动的 SPD、DDR4/5 基础 CRC/组织/基础时序；AMD UMC 212 字段与离线快照 | UMC 各型号物理单位/通道标签；Intel 训练时序；PMIC/VRM/EC/时钟芯片和训练写入 |

入口与代码：[平台控制](platform-controls.md)、[Intel offset](intel-oc-recovery.md)、
[UMC](amd-umc-recovery.md)、[AMD 拓扑与 PStates](amd-topology-recovery.md)。
安装包与精确源码/验证链接统一见[产物记录](artifacts.md)。

## 已确定的剩余障碍

PStates 原设置入口遗漏第 9 位 VID，并把返回的 64 位 processor mask 截为 32 位。
AMD PPR 对 VID 和其它字段又规定了不同的跨核范围，Model 02h 手册不能认证 9995WX 的全部行为。
现有证据无法把原电压公式和单核设置循环直接视为正确的新实现。

Intel client offset 与最大 OC ratio 的数据布局和接口已经形成限定实现；GNR 的 13 位目标选择器来自另一个对象表，
NVL 也有独立按核心表。缺少的是这些表的完整目标绑定与相应固件命令契约，不能由 W790/W890 板名填出。
原 VF 操作会连带改变 per-core override，后续必须明确这些附带状态。

AMD [六字段完整槽实验](amd-limits-recovery.md)确认大多数字段走 MP1，FIT 的一部分路径才走 BIOS。
原乘法会回绕、部分消息重用、失败仍提示 Applied；六个入口标签和第二个对象标志已由原 UI/构造指令补齐，
仍需要完成 MP1 隐含写入、参数语义和返回校验；[曲线查询](amd-curve-query-recovery.md)已有原指令实验与独立实现；[目标列表和设置入口](amd-curve-target-mapping.md)的静态追踪进一步区分了 PM 表编号与物理核心。
把同名 BIOS 命令换上这些输入单位，并不足以还原原硬件行为。

内存与主板寄存器依赖内存代际、通道/插槽路由、芯片 ID 和固件状态；当前 UMC 先按原编码显示。
板级写入仍缺少各芯片的确认、访问协调及可验证的更新/恢复规则。

作者暂时不能采集 Linux 真机，继续采用原 ELF、Windows 参考和公开资料作离线研究。
本文没有新增采集要求；待已有条件变化时，按[既有真机清单](hardware-acceptance.md)逐平台验收。

## 已验证与未验证的边界

Linux 已建立 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、Rocky Linux 8/9/10 的构建、
GUI 安装、授权和目标内核 VM 验证；驱动仍为 DKMS 针对实际内核构建的 `.ko`。
Windows/macOS 当前验证独立核心，完整 GUI/硬件后端的目标仍是 Linux。
四台目标机器的寄存器行为、物理电压/温度效果、Secure Boot 登记和真实桌面交互认证尚未验收。
