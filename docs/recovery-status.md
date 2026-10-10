# 功能还原状态

<!-- ddr5-xmp-4932f1b -->
2026-10-10：最新验证生产提交 `4932f1b`，GUI 与 CLI 同步新增 **DDR5 XMP 3.0 厂家档案读取**。
显示三个档案的名称、电压和原始时序，分别检查头/档案 CRC，识别 EXPO 重叠区域。
档案值不代表当前训练值；没有 SPD 写入或配置应用。

- [实现说明](ddr5-xmp-recovery.md)、[功能对照](frontend-progress.md)、[验证交付](ddr5-xmp-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38048131383) 4/4，各 12 CTest / 111 场景组，新增 SPD ASan/UBSan 检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38048131391) 12/12，十目标各 14 CTest、268 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38048131414) 23/23，十目标各 59 Qt 测试、11 套内核 VM。
- `dist/gui-cli-ddr5-xmp-4932f1b/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参与稳定性尚未验收。以下保留此前记录。

<!-- intel-uncore-94b0655 -->
2026-10-10：最新验证生产提交 `94b0655`，GUI 与 CLI 同步新增 **Intel Ring/LLC 倍率范围**。
限定 Raptor Lake-S B7、Sapphire Rapids 8F；最小、最大倍率一起提交，保留其它位，检查旧值并完整读回。
相同值不重复写入，未测量实际频率或调参稳定性。

- [实现说明](intel-uncore-recovery.md)、[功能对照](frontend-progress.md)、[验证交付](intel-uncore-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38044299378) 4/4，各 11 CTest / 105 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38044299343) 12/12，十目标各 13 CTest、259 份模拟 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38044299384) 23/23，十目标各 57 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-uncore-94b0655/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参与稳定性尚未验收。以下保留此前记录。

<!-- umc-offline-672422a -->
2026-10-10：最新验证生产提交 `672422a`，GUI 与 CLI 同步新增 **UMC 离线快照导入与比较**。
GUI 保存的快照可以在 CLI 解码；两端共用 212 字段比较，区分字段值、原始字变化与数据缺失。
无需硬件访问，支持部分离线快照；文件身份和物理通道对应关系仍未经验证。

- [使用说明](umc-offline.md)、[功能对照](frontend-progress.md)、[验证交付](umc-offline-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38039881978) 4/4，各 10 CTest / 98 场景组，另有 ASan/UBSan 检查。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38039885515) 12/12，十目标各 12 CTest、230 份模拟 JSON 报告及离线实际进程检查。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38039881391) 23/23，十目标各 54 Qt 测试、11 套内核 VM。
- `dist/gui-cli-umc-offline-672422a/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参与稳定性尚未验收。以下保留此前记录。

<!-- intel-vf-edit-32c89ae -->
2026-10-10：最新验证生产提交 `32c89ae`，GUI 与 CLI 同时新增 **Intel V/F 单点偏移设置**。
支持 B7 client 的 core/cache，明确选择单点；提交前核对配置，提交后复核完整点值与上下文。
只在锁和配置条件允许时开放，不自动改变全域电压或 per-core override。

- [实现及来源](intel-vf-write-recovery.md)、[功能对照](frontend-progress.md)、[验证与交付](intel-vf-edit-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38032688225) 4/4，各 9 CTest / 90 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38032688257) 12/12，十目标各 11 CTest、220 份 JSON。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38032688308) 23/23，十目标各 51 Qt 测试、11 套内核 VM。
- `dist/gui-cli-vf-edit-32c89ae/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码与 SHA256SUMS。

仍有其它型号及功能缺口；未进行目标真机调参或稳定性验收。以下保留此前记录。

<!-- intel-turbo-e43bad7 -->
2026-10-10：最新验证生产提交 `e43bad7`，GUI 与 CLI 同时新增 **Intel P/E 睿频分组**。
读取八组倍率和活动核心数量阈值，支持选定组倍率设置、旧值检查与完整读回；
当前限定 family 6/model B7，保留核心数量阈值，不视为逐物理核心设置。

- [实现说明](intel-turbo-recovery.md)、[功能对照](frontend-progress.md)、[验证及交付](intel-turbo-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38028784706) 4/4，各 8 CTest / 81 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38028751967) 12/12，十目标各 10 CTest、180 份 JSON 验证。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38028751963) 23/23，十目标各 47 Qt 测试、11 套内核 VM。
- `dist/gui-cli-turbo-e43bad7/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码和 SHA256SUMS。

原版全部功能与四台真机验收仍未完成；驱动/HAL/ABI 未改，模拟测试不等于硬件调参验收。
以下保留此前记录。

<!-- controls-register-c0f97cc -->
2026-10-10：最新验证生产提交 `c0f97cc`，GUI 与 CLI 同步更新。
新增 **HWP 活动窗口**，全部 13 项 RAPL/HWP 设置检查完整配置读回；
CLI 补齐原始 MSR/MMIO/PCI 读写，GUI 可离线导入 CLI 的 UMC 报告并重算 212 字段。

- [实现说明](control-register-recovery.md)、[功能对照](frontend-progress.md)、[验证及交付](control-register-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38023681945) 4/4，各 7 CTest / 72 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38023680671) 12/12，十目标各 9 CTest、147 份 JSON 验证。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38023680720) 23/23，十目标各 43 Qt 测试、11 套内核 VM。
- `dist/gui-cli-controls-c0f97cc/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码和 SHA256SUMS。

AMD 写入协议的[交叉核对](amd-shimada-crosscheck.md)另有记录。原版全部功能与四台真机验收仍未完成；
驱动/HAL/ABI 未改，模拟测试不等于硬件调参验收。以下保留此前记录。

<!-- intel-voltage-452f594 -->
2026-10-10：GUI 与独立 CLI 同时新增 **Intel 目标电压 + Adaptive/Override 模式设置**，验证生产提交 `452f594`。
沿用 Raptor Lake-S B7 的 core/cache 配置，明确选择单一域，保留 offset 与倍率；
旧值检查、固件状态和完整回读一致后才显示验证成功。目标配置不代表实测电压。

- [实现与研究](intel-voltage-recovery.md)、[GUI/CLI 功能对照](frontend-progress.md)、[验证及交付](intel-voltage-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38020601340) 4/4；各 7 CTest / 67 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38020601346) 12/12；十目标各 9 CTest，91 份 JSON 验证。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38020601328) 23/23；十目标各 38 Qt 测试、11 套内核 VM。
- `dist/gui-cli-voltage-452f594/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码与 SHA256SUMS。

驱动/HAL/ABI 未改；VF 点写入、Intel server 电压域及四台目标真机验收仍未完成。以下保留此前记录。

<!-- intel-vf-544f0b6 -->
2026-10-10：GUI 与独立 CLI 同时新增 **Intel V/F 点查询**，验证生产提交 `544f0b6`。
支持现有 Raptor Lake-S B7 profile 的 core/cache 单点或 1..15 候选点读取，显示配置倍率、offset、
原始数据与逐点错误；未开放 V/F 写入、逐核 override 或 Xeon server 配置。

- [实现与研究](intel-vf-recovery.md)、[GUI/CLI 功能对照](frontend-progress.md)、[验证及交付](intel-vf-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38018798924) 4/4；各 7 CTest / 62 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38018796517) 12/12；十目标各 9 CTest，65 份 JSON 输出验证。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38018795844) 23/23；十目标各 35 Qt 测试、11 套内核 VM。
- `dist/gui-cli-vf-544f0b6/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码与 SHA256SUMS。

驱动/HAL/ABI 未改；四台真机尚未验收。以下保留此前记录。

<!-- headless-cli-b2ef0a3 -->
2026-10-10 无界面入口已增加：`octool-cli`，验证源码 `b2ef0a3`。
它复用本页已有的核心功能边界，提供 SSH/终端操作和 JSON 输出，不新增未还原的硬件语义。
主板/传感器/SPD 读取移入 GUI、CLI 共用的无 Qt 实现。
[使用说明](headless-cli.md)、[验证记录](headless-cli-validation.md)：十个发行版各 9 CTest，独立 CLI 安装/运行/卸载重装通过，既有 Linux 23/23 回归通过。
PStates 写入、曲线写入、Intel server VF/fabric、板级专用控制等缺口及四台真机验收仍保留。

以下保留此前记录。

`06b477c` 是此前 [Shimada 原始曲线查询](amd-curve-query-recovery.md)的历史版本，
[当时验证](validation/amd-curve-recovery-ci-06b477c.json)为核心 4/4、Linux 23/23。
最新生产版本以本页顶部为准，下表已更新到 `4932f1b`。
原版全部功能仍未完成。安装兼容、自动化回归、原指令研究和真实硬件验收分别记录，
没有能代表四大模块全部功能的可靠百分比；212 个 UMC 字段也不是整个软件的功能分母。

| 用户要求的模块 | 已接入可编译程序 | 尚未还原的部分 |
|---|---|---|
| AMD PStates | 按逻辑 CPU 采样 P0–P7、上限/能力检查、频率、64 位原值、完整 9 位 VID / Idd 编码 | 型号专用 mV/A 换算；满足跨核/跨 coherent fabric 一致性的设置 |
| Intel Controls | RAPL、HWP 含活动窗口，13 项设置完整读回；温度；Raptor Lake-S core/cache offset、最大 OC ratio、目标电压与 Adaptive/Override；VF 点查询及受条件限制的单点 offset；P/E 睿频分组读取及单组倍率设置；B7/8F Ring/LLC 最小、最大倍率范围 | W790/W890 电压域；其它型号 VF、逐物理核心 override、活动核心阈值编辑、TVB、VID rank/SP、fabric/BCLK |
| AMD 调参 | 三套独立 BIOS SMUIO；Shimada 已提取命令和逐核频率参数准备；CPUID 稀疏拓扑；限定身份的原始曲线查询 | CPUID 与固件目标 ID 对应；MP1/PBO 高层单位、曲线设置/电流/温度限制、PM 表、profiles/hotkeys |
| 内存与主板 | DMI/BIOS、内核传感器、绑定驱动的 SPD、DDR4/5 基础 CRC/组织/基础时序；DDR5 XMP 3.0 厂家档案名称、电压、时序及逐区 CRC；AMD UMC 212 字段、GUI/CLI 快照双向离线导入及同 bank/slot 比较 | EXPO / XMP 用户档案 / DDR4 XMP；UMC 各型号物理单位/通道标签；Intel 训练时序；PMIC/VRM/EC/时钟芯片和训练写入 |

入口与代码：[平台控制](platform-controls.md)、[Intel offset](intel-oc-recovery.md)、
[UMC](amd-umc-recovery.md)、[AMD 拓扑与 PStates](amd-topology-recovery.md)。
安装包与精确源码/验证链接统一见[产物记录](artifacts.md)。

## 已确定的剩余障碍

PStates 原设置入口遗漏第 9 位 VID，并把返回的 64 位 processor mask 截为 32 位。
AMD PPR 对 VID 和其它字段又规定了不同的跨核范围，Model 02h 手册不能认证 9995WX 的全部行为。
现有证据无法把原电压公式和单核设置循环直接视为正确的新实现。

Intel client offset 与最大 OC ratio 的数据布局和接口已经形成限定实现；GNR 的 13 位目标选择器来自另一个对象表，
NVL 也有独立按核心表。缺少的是这些表的完整目标绑定与相应固件命令契约，不能由 W790/W890 板名填出。
原 VF 操作会连带改变 per-core override；当前单点设置保留模式并核对上下文，逐物理核心模式仍未恢复。

AMD [六字段完整槽实验](amd-limits-recovery.md)确认大多数字段走 MP1，FIT 的一部分路径才走 BIOS。
原乘法会回绕、部分消息重用、失败仍提示 Applied；六个入口标签和第二个对象标志已由原 UI/构造指令补齐，
仍需要完成 MP1 隐含写入、参数语义和返回校验；[曲线查询](amd-curve-query-recovery.md)已有原指令实验与独立实现；[目标列表和设置入口](amd-curve-target-mapping.md)的静态追踪进一步区分了 PM 表编号与物理核心。
把同名 BIOS 命令换上这些输入单位，并不足以还原原硬件行为。
[Shimada 外部实现交叉核对](amd-shimada-crosscheck.md)进一步记录了 setter 访问端口、
PBO 命令表与型号支持差异；这些差异尚未形成可验证的新增写入契约。

内存与主板寄存器依赖内存代际、通道/插槽路由、芯片 ID 和固件状态；当前 UMC 先按原编码显示。
板级写入仍缺少各芯片的确认、访问协调及可验证的更新/恢复规则。

作者暂时不能采集 Linux 真机，继续采用原 ELF、Windows 参考和公开资料作离线研究。
本文没有新增采集要求；待已有条件变化时，按[既有真机清单](hardware-acceptance.md)逐平台验收。

## 已验证与未验证的边界

Linux 已建立 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、Rocky Linux 8/9/10 的构建、
GUI 安装、授权和目标内核 VM 验证；驱动仍为 DKMS 针对实际内核构建的 `.ko`。
Windows/macOS 当前验证独立核心，完整 GUI/硬件后端的目标仍是 Linux。
四台目标机器的寄存器行为、物理电压/温度效果、Secure Boot 登记和真实桌面交互认证尚未验收。
