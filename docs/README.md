# 项目知识入口

<!-- amd-oc-cores-8c85a28 -->
2026-10-21：最新生产验证提交 `8c85a28`，GUI / CLI 同步新增
**同一 CCD 内显式核心列表的频率设置**。每核重查能力与身份，首错停止并报告部分完成状态。

- [用法](amd-manual-oc.md)、[整批验证](amd-oc-cores-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1359 JSON、GUI 各 212 项、核心 422 场景组；12 次内核 VM。
- `dist/gui-cli-amd-oc-cores-8c85a28/`：33 个包、精确云端源码、更新用户源码及校验表。
- [REA 研究](amd-oc-cores-research.md)：八个原函数、768 组原指令边界实验；不沿用原版无条件八核循环。
- 频率只有固件确认，物理映射/真机验收未验证；逐核/CCD 遥测、rails、动态内存/EC 及其它平台仍未完成。

以下保留历史记录。
<!-- /amd-oc-cores-8c85a28 -->

<!-- el7-recovery-bc30c4f -->
2026-10-13：本批已验证提交 `bc30c4f`，新增 **EL7（Oracle Linux 7.9 x86_64）**，并同步恢复 GUI / CLI 的 AVX2 偏移、TCC 温控、限频原因与历史清除、最大非睿频倍率和封装热事件。

- [EL7 安装与范围](el7-port.md)、[整批验证交付](el7-recovery-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611258) 4/4：各 22 CTest、291 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611273) 13/13：11 目标各 24 CTest、794 份 JSON、32 场景组。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611277) 26/26：11 目标及两个 Qt 基线各 135 项 GUI 测试，12 套匹配内核 VM。
- `dist/gui-cli-el7-recovery-bc30c4f/`：33 个安装包、固定提交源码和 SHA256。

ICCmax/VR 供电域、增强 TVB/Vmax、部分 AMD 与主板调参仍在恢复；[ICCmax 新证据](intel-iccmax-research.md)没有计作已开放功能。真机调参和 Secure Boot 尚未验收。以下为历史记录。
<!-- /el7-recovery-bc30c4f -->

<!-- intel-energy-61a0494 -->
2026-10-13：最新验证生产提交 `61a0494`，GUI / CLI 同步恢复 **Intel 节能睿频与 Race to Halt**。
B7 裸机；两项独立启用/禁用，完整旧值检查、保留其它位、完整读回。

- [实现范围](intel-energy.md)、[验证交付](intel-energy-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38323413941) 4/4，各 19 CTest、268 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38323414012) 12/12，十目标各 21 CTest、657 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38323413900) 23/23，十目标各 115 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-energy-61a0494/`：30 个安装包、源码及 SHA256。

增强 TVB、Vmax、逐物理核心表及其它原版缺口和真机验收仍未全部完成。以下保留历史记录。
<!-- /intel-energy-61a0494 -->

<!-- intel-ring-c6e9599 -->
2026-10-12：最新验证生产提交 `c6e9599`，GUI / CLI 同步恢复 **Intel Ring DownBin**。
B7 裸机；独立启用/禁用，含锁、旧值和完整回读检查，保留 TVB / Vmax 等其它位。

- [实现范围](intel-ring-downbin.md)、[验证交付](intel-ring-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38258982087) 4/4，各 18 CTest、256 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38258988264) 12/12，十目标各 20 CTest、585 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38258946852) 23/23，十目标各 109 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-ring-c6e9599/`：30 个安装包、源码及 SHA256。

Vmax、逐物理核心表、其它原版缺口及真机验收仍未全部完成。以下保留历史记录。
<!-- /intel-ring-c6e9599 -->

<!-- intel-tvb-edit-494b8f0 -->
2026-10-12：最新验证生产提交 `494b8f0`，GUI / CLI 同步恢复 **Intel TVB 分档单字段编辑**。
B7 裸机；温度 A/B、降倍率 A/B，保守范围内单项设置，含旧值核对和完整读回。

- [实现范围](intel-tvb-table-edit.md)、[验证交付](intel-tvb-edit-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38238262625) 4/4，各 18 CTest、255 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38238267459) 12/12，十目标各 20 CTest、564 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38238260581) 23/23，十目标各 108 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-tvb-edit-494b8f0/`：30 个安装包、源码及 SHA256。

逐物理核心表、其它原版缺口及真机验收仍未全部完成。以下保留历史记录。

<!-- intel-tvb-table-aca1652 -->
2026-10-12：最新验证生产提交 `aca1652`，GUI / CLI 同步恢复 **TVB 温度与降倍率分档查询**。
限定 Intel 06_B7 裸机；支持单项或八项候选扫描，显示两组温度、降倍率及逐项错误。

- [实现范围](intel-tvb-table.md)、[验证交付](intel-tvb-table-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38225310065) 4/4，各 244 场景组；含 8 组新表查询。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38225315230) 12/12，十目标各 19 CTest、520 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38225308264) 23/23，十目标各 102 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-tvb-table-aca1652/`：30 个安装包、源码及 SHA256。

分档写入、逐物理核心表、其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-tvb-743c40f -->
2026-10-12：最新验证生产提交 `743c40f`，GUI / CLI 同步恢复 **TVB 高温倍频裁剪与电压优化开关**。
限定 Intel 06_B7 裸机；检查锁、旧配置、错误码和完整回读，保留其它位。

- [实现范围](intel-tvb.md)、[验证交付](intel-tvb-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38219382818) 4/4，各 236 场景组；含 10 组 TVB。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38219387307) 12/12，十目标各 18 CTest、493 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38219380980) 23/23，十目标各 98 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-tvb-743c40f/`：30 个安装包、源码及 SHA256。

TVB 分档表、其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-refresh-97c1b22 -->
2026-10-12：最新验证生产提交 `97c1b22`，GUI / CLI 同步增加 **Intel 刷新、自刷新与 ZQ 参数**。
限定 B7/A700，18 寄存器 / 57 字段；按类型显示别名及单位，兼容旧 v1/v2 快照。

- [实现范围](intel-memory-refresh.md)、[验证交付](intel-refresh-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38208752141) 4/4，各 226 场景组；23 组 Intel 内存及内存安全检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38208752189) 12/12，十目标各 17 CTest、462 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38208752202) 23/23，十目标各 93 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-refresh-97c1b22/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-clock-b3a40d7 -->
2026-10-12：最新验证生产提交 `b3a40d7`，GUI / CLI 同步新增 **Intel BCLK 与最后一次 MRC 请求**。
限定 B7/A700，三个共享 PCU 寄存器和独立快照；历史训练请求不代表实时内存频率。

- [实现范围](intel-clock.md)、[验证交付](intel-clock-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38200818742) 4/4，各 221 场景组；12 组 Intel 时钟及内存安全检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38200822292) 12/12，十目标各 17 CTest、450 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38200800678) 23/23，十目标各 91 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-clock-b3a40d7/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-extended-ec6077e -->
2026-10-12：最新验证生产提交 `ec6077e`，GUI / CLI 同步补充 **Intel 各 Rank RTL 与掉电时序**。
限定 B7/A700，扩展为 14 寄存器 / 43 字段，按字段保留时钟单位，兼容 v1 快照。

- [实现范围](intel-memory-extended.md)、[验证交付](intel-extended-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38185638950) 4/4，各 209 场景组；18 组 Intel 内存及内存安全检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38185638958) 12/12，十目标各 16 CTest、430 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38185638884) 23/23，十目标各 86 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-extended-ec6077e/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-memory-001c804 -->
2026-10-12：最新验证生产提交 `001c804`，GUI / CLI 同步新增 **Intel Raptor Lake-S 内存时序读取与快照**。
限定 B7/A700，读取 30 字段；核对空通道、两次采样和 Gear 状态，支持两端离线快照互通。

- [实现范围](intel-memory.md)、[验证交付](intel-memory-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38148549524) 4/4，各 205 场景组；SPD / PM / UMC / Intel 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38148554697) 12/12，十目标各 16 CTest、422 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38148404619) 23/23，十目标各 84 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-memory-001c804/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- ddr4-asymmetric-57924bb -->
2026-10-11：最新验证生产提交 `57924bb`，GUI / CLI 同步完成已确认的 **DDR4 非对称容量**：
SDP/DDP rank 分配、离散密度等级与两组 3DS 层数分别计算，显示容量分项和合计。

- [解码范围](ddr4-asymmetric.md)、[验证交付](ddr4-asymmetric-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38143970745) 4/4，各 191 场景组；SPD / PM / UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38143970568) 12/12，十目标各 15 CTest、402 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38143970549) 23/23，十目标各 79 Qt 测试、11 套内核 VM。
- `dist/gui-cli-ddr4-asymmetric-57924bb/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- mixed-turbo-6433918 -->
2026-10-11：最新验证生产提交 `6433918`，GUI / CLI 同步完成 **Intel 中间组活动核心阈值编辑**
和 **DDR5 混合 rank 容量分项**。保留末组阈值和全部倍率；两组内存分别使用自身位宽和堆叠层数。

- [阈值规则](turbo-thresholds.md)、[混合内存](spd-asymmetric.md)、[验证交付](mixed-turbo-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38140350467) 4/4，各 185 场景组；SPD / PM / UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38140353378) 12/12，十目标各 15 CTest、392 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38140349232) 23/23，十目标各 77 Qt 测试、11 套内核 VM。
- `dist/gui-cli-mixed-turbo-6433918/`：30 个安装包、源码及 SHA256 校验文件。

原版全部功能及目标真机验收尚未完成。以下保留历史记录。

<!-- amd-pm-62e7bd8 -->
2026-10-11：最新验证生产提交 `62e7bd8`，GUI / CLI 同步新增 **AMD PM 遥测与快照互通**。
八个精确版本、24 项公共字段；支持原始表和 JSON 离线导入，并可读取已有兼容驱动。

- [实现说明](amd-pm-recovery.md)、[功能对照](frontend-progress.md)、[验证交付](amd-pm-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38135506440) 4/4，各 173 场景组；PM / SPD / UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38135503372) 12/12，十目标各 15 CTest、362 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38135499975) 23/23，十目标各 73 Qt 测试、11 套内核 VM。
- `dist/gui-cli-amd-pm-62e7bd8/`：30 个安装包、源码及 SHA256 校验文件。

完整 PM 表、原生传输、其余调参功能及真机验收尚未完成。以下保留历史记录。

<!-- spd-user-enhanced-75c5c8b -->
2026-10-11：最新验证生产提交 `75c5c8b`，GUI / CLI 同步增加 **XMP 3.0 用户槽读取与 EXPO 1.0 增强时序存储值**。
独立检查捕获状态和 CRC，支持有效数据的零 CRC；用户槽与增强功能的启用状态仍为未知。

- [实现说明](spd-user-enhanced.md)、[功能对照](frontend-progress.md)、[验证交付](spd-user-enhanced-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38130003195) 4/4，各 154 场景组；SPD/UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38130005949) 12/12，十目标各 14 CTest、342 份 JSON。
- [GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38130002165) 23/23，十目标各 68 Qt 测试、11 套内核 VM。
- `dist/gui-cli-spd-user-enhanced-75c5c8b/`：30 个安装包、源码及 SHA256 校验文件。
- [AMD PM 表后续研究](amd-pm-table-followup.md)固定八个版本和部分字段，尚未接入实时读取。

原版全部功能与真机调参验收尚未完成。以下保留历史记录。

<!-- spd-profile-geometry-6f544e6 -->
2026-10-11：最新验证生产提交 `6f544e6`，GUI / CLI 同步补齐 **XMP 2.0 CAS/推荐 DIMM 数、对称堆叠容量与 DDR5 3DS 刷新要求**。
同时修正 DDR5 四/八子通道计数，并按基础/公共区修订版判断可解释的组织字段。

- [实现说明](spd-profile-geometry.md)、[功能对照](frontend-progress.md)、[验证交付](spd-profile-geometry-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38125934367) 4/4，各 145 场景组；SPD / UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38125934384) 12/12，十目标各 14 CTest、327 份 JSON。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38125934404) 23/23，十目标各 66 Qt 测试、11 套内核 VM。
- `dist/gui-cli-spd-profile-geometry-6f544e6/`：30 个安装包、源码及 SHA256 校验文件。

原版全部功能与真机调参验收尚未完成。以下保留历史记录。

<!-- spd-base-timings-88530a9 -->
2026-10-11：最新验证生产提交 `88530a9`，GUI 与 CLI 同步补充 **DDR4 写延迟和 DDR5 基础次级时序**。
DDR4 新增 3 项；DDR5 新增 8 对 ps / nCK 要求，SPD 1.2 起再增加 3 对，按基础修订版判断。
这些是存储要求，不表示当前训练参数，尚未应用或写入配置。

- [实现说明](spd-base-timings.md)、[功能对照](frontend-progress.md)、[验证交付](spd-base-timings-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38123233835) 4/4，各 12 CTest / 137 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38123233886) 12/12，十目标各 14 CTest、313 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38123233832) 23/23，十目标各 64 Qt 测试、11 套内核 VM。
- `dist/gui-cli-spd-base-timings-88530a9/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

<!-- spd-cas-6aca371 -->
2026-10-11：最新验证生产提交 `6aca371`，GUI 与 CLI 同步新增 **SPD CAS 支持列表与 XMP 3.0 命令率**。
读取 DDR4/DDR5 基础 CAS 和三组 XMP 3.0 CAS/命令率；保留原始字节、未知代码及校验状态。
这些是档案要求，不表示当前训练参数，尚未应用或写入档案。

- [实现说明](spd-cas-recovery.md)、[功能对照](frontend-progress.md)、[验证交付](spd-cas-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38121792862) 4/4，各 12 CTest / 131 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38121792949) 12/12，十目标各 14 CTest、301 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38121792834) 23/23，十目标各 63 Qt 测试、11 套内核 VM。
- `dist/gui-cli-spd-cas-6aca371/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

<!-- xmp-secondary-84f21bc -->
2026-10-11：最新验证生产提交 `84f21bc`，GUI 与 CLI 同步新增 **DDR5 XMP 3.0 次级时序与周期下限**。
三组厂家档案各追加 8 对 ps / nCK 要求值；保留 CRC、独立启用及异常隔离。
档案值不表示当前训练结果，尚未应用或写入档案。

- [实现说明](ddr5-xmp-recovery.md)、[功能对照](frontend-progress.md)、[验证交付](xmp-secondary-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38100545606) 4/4，各 12 CTest / 126 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38100545582) 12/12，十目标各 14 CTest、293 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38100545584) 23/23，十目标各 62 Qt 测试、11 套内核 VM。
- `dist/gui-cli-xmp-secondary-84f21bc/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

<!-- ddr4-xmp-b1b9adf -->
2026-10-11：最新验证生产提交 `b1b9adf`，GUI 与 CLI 同步新增 **DDR4 XMP 2.0 档案读取**。
两组档案的电压、12 项时序、独立启用/时间基准、有符号修正和异常隔离已接通。
XMP 2.0 无扩展 CRC，界面与 JSON 明确标注；未应用或写入档案。

- [实现说明](ddr4-xmp-recovery.md)、[功能对照](frontend-progress.md)、[验证交付](ddr4-xmp-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38099021180) 4/4，各 12 CTest / 122 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38099021189) 12/12，十目标各 14 CTest、288 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38099021214) 23/23，十目标各 61 Qt 测试、11 套内核 VM。
- `dist/gui-cli-ddr4-xmp-b1b9adf/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

<!-- ddr5-expo-e4cb81b -->
2026-10-10：最新验证生产提交 `e4cb81b`，GUI 与 CLI 同步新增 **DDR5 EXPO 1.0 基础档案读取**。
两组档案的三项电压、十项时序、独立启用状态及整块 CRC 已接通；XMP/EXPO 错误分区保留。
档案值不代表当前训练值；没有 SPD 写入或配置应用。

- [实现说明](ddr5-expo-recovery.md)、[功能对照](frontend-progress.md)、[验证交付](ddr5-expo-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38056197597) 4/4，各 12 CTest / 116 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38056197668) 12/12，十目标各 14 CTest、278 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38056197685) 23/23，十目标各 60 Qt 测试、11 套内核 VM。
- `dist/gui-cli-ddr5-expo-e4cb81b/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

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
2026-10-10：新增独立无桌面版 **`octool-cli`**，验证提交 `b2ef0a3`，分支 `refactor/platform-recovery`。
不依赖 Qt、X11、Wayland 或图形授权，可在 SSH/本地终端调用现有已还原核心。
提供诊断、CPU/PStates/Intel/AMD/UMC 查询、主板/传感器/SPD 及受限设置命令；设置需要明确 `--apply`。

- [无界面使用说明](headless-cli.md)、[本轮验证与大小](headless-cli-validation.md)、[证据清单](validation/headless-cli-ci-b2ef0a3.json)。
- [CLI 云端验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38016466064) 12/12，十目标各 9 CTest，无显示环境安装、普通用户诊断、卸载重装通过。
- [既有 Linux 回归](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38016466047) 23/23，每目标 32 Qt 测试，11 套目标内核 VM 启动通过。
- CLI 包 138,156–173,404 字节，可执行文件 324,256–380,240 字节；不含可选 DKMS 和系统运行库。
- `dist/headless-cli-b2ef0a3/` 含 10 个 CLI + 10 个独立 DKMS 包、对应源码和 SHA256SUMS。

GUI 与 CLI 共用硬件核心和无 Qt 清单读取；驱动/HAL/ABI 未变。CLI 没有扩大硬件支持范围，四台真机仍未验收。

以下保留此前记录。

<!-- amd-curve-recovery-06b477c -->
2026-10-10：最新验证生产代码为 `06b477c`，研究分支 `refactor/platform-recovery`。
新增限定 Shimada 身份的[原始曲线查询](amd-curve-query-recovery.md)：独立 B8/BC 通道、有界等待、严格返回检查和界面失效清理。
手动填写固件 CCD/core；保留 32 位原值，不推导物理核心映射或 mV，不设置曲线。

- [核心验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38013841095) 4/4，各 7 CTest / 56 场景组；[Linux 验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38013841147) 23/23，十目标各 32 Qt / 32 Python。
- 新增 98 组原构造、71 组原查询实验；分析测试共 65 项。11 套内核 VM、20 个 DEB/RPM、200 张截图已归档。
- [验证明细](validation/amd-curve-recovery-ci-06b477c.json)、[安装产物](artifacts.md)、[剩余还原项](recovery-status.md)。四台真机验收仍未完成。

以下保留此前验证记录。

<!-- intel-ratio-recovery-fa39af6 -->
2026-10-10：最新验证生产代码为 `fa39af6`，研究分支 `refactor/platform-recovery`。
新增 Raptor Lake-S 核心/缓存最大 OC 倍频设置；保留电压 offset、target、mode，检查锁/旧值并完整回读。
原版全部功能和四台真机验收仍未完成，范围见[还原状态](recovery-status.md)与[倍频实现](intel-ratio-recovery.md)。

- [核心验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38011778227) 4/4，各 6 CTest / 49 场景组；[Linux 验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38011778246) 23/23，十目标各 29 Qt / 31 Python。
- 11 套内核 VM、20 个 DEB/RPM、200 张界面截图及源码校验已归档：[验证明细](validation/intel-ratio-recovery-ci-fa39af6.json)、[安装产物](artifacts.md)。
- 原 Intel 倍频 108 场景、AMD 六字段 882 场景、六行标签绑定与 80 组标志分支均进入本次云端研究门禁；不属于真实硬件调参验证。

以下保留此前验证记录。

<!-- intel-oc-recovery-90e16fb -->
2026-10-10：最新验证生产代码为 `90e16fb`，分支 `refactor/platform-recovery`。
在 UMC 212 字段、快照文件和 AMD 拓扑基础上，新增 Raptor Lake-S core/cache 电压 offset：
分别选域、保持其它字段、检查锁和旧值、有限等待与写后回读。
功能边界与剩余工作统一见[还原状态](recovery-status.md)，实现见[Intel offset](intel-oc-recovery.md)。

- [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38009434518) 23/23，通过十目标；每目标 27 Qt / 31 Python，11 套目标内核 VM 启动。
- [独立核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38009434514) 4/4，各 6 CTest / 45 场景组。
- 20 份 DEB/RPM、源码包和校验表已归档至 `dist/intel-oc-recovery-90e16fb/`；[完整结果](validation/intel-oc-recovery-ci-90e16fb.json)含 200 张截图及源码/产物哈希。
- [AMD 六字段研究](amd-limits-recovery.md)另有 882 项本地原指令实验和 3 项回归；它们没有被算入上述生产代码的云端测试数。

原版全部功能仍未完成：PStates 设置、Intel server 电压域 / VF / fabric、AMD 完整 PBO/MP1 高层调参，
以及 Intel 训练时序与板级 PMIC/VRM/EC/时钟写入仍有缺口。四台目标机器尚无真机验收。

以下为此前记录；最新生产代码及验收结果以上述版本为准。

<!-- umc-recovery-728671d -->
2026-10-10：新增 AMD UMC 212 字段 / 12 分组、显式地址组和刷新槽、离线 JSON 快照，
以及 AMD 扩展 CPUID 拓扑读取；验证源码为 `728671d`。
原构造的通道搜索和同名字段问题已从原指令复现，重构版不会沿用错误地址或以名称覆盖字段。
功能说明见 [UMC](amd-umc-recovery.md)、[CPU 拓扑 / PStates 剩余问题](amd-topology-recovery.md)。

- [Linux 验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38007763354) 23/23；十目标各 24 Qt / 30 Python，11 套目标内核 VM 启动。
- [核心验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38007763396) 四环境通过，各 5 CTest / 35 场景组。
- 20 份 DEB/RPM、源码包及 SHA256SUMS 已保存于 `dist/umc-recovery-728671d/`；[逐项报告](validation/umc-recovery-ci-728671d.json)包含 180 张截图及产物哈希。

PStates 写入、Intel 电压 / VF、AMD 完整调参和主板专用写入仍未完成；
UMC 当前显示原始编码，没有把型号未确认的字段换算为周期。四台目标机器尚无真机验收。

以下保留此前记录；本次新增功能与验证以以上链接为准。


2026-10-10 平台恢复增量已接入，验证代码为 `0b514bb`（分支 `refactor/platform-recovery`）。
AMD PStates 增加完整 VID/Idd 原始字段；Intel Controls 增加 RAPL/HWP 读写和温度；
AMD 增加受限 BIOS SMUIO 与 CCD/core/MHz 命令准备；内存与主板页增加 DMI、hwmon、
驱动已暴露的 SPD、DDR4/DDR5 基础 CRC 和 SPD 时序解码。
**仍未完成原版全部功能**：PStates 设置/物理电压电流、Intel VF/逐核 turbo/fabric、
AMD 完整调参/拓扑与曲线、运行时内存时序及 PMIC/VRM/EC/板载时钟仍有缺口。
详细范围见[本轮功能表](platform-controls.md)，不能把测试通过当作四台目标机器的硬件验收。

- [Linux 完整验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068664) 23/23，通过十个发行版目标；每目标 21 Qt / 29 Python，11 套内核 VM 启动。
- [独立核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068831) 四环境全部通过，每环境 4 CTest / 26 场景组。
- [本轮证据与哈希](validation/platform-recovery-ci-0b514bb.json)；20 份 DEB/RPM 与源码包位于
  `dist/platform-recovery-0b514bb/`，参见[产物说明](artifacts.md)。

以下保留此前记录；最新功能和验证以本段及其链接为准。

2026-10-09 现有重构组件的十目标 Linux 兼容适配及自动化验收已完成，验证提交 `88999aa`，分支 `refactor/linux-integration`。
范围为现有重构版的基础信息、原始 MSR/MMIO/PCI、AMD PStates 只读页，以及其运行依赖、
启动器、独立授权辅助程序、polkit 与 DKMS 交付。原版其余调参面板仍未全部恢复。

- [完整云端验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37923477646)：23/23 成功；Ubuntu20.04/22.04/24.04/26.04、Debian11/12/13、Rocky8/9/10。
- 11 套真实发行版内核在 QEMU 中启动，模块加载、双 CPU 的 HAL/CPUID、设备权限、无效请求及卸载通过。
- EL8 基线与十目标各 17 项 Qt 回归、每目标 28 项 Python 回归通过，无跳过。
- 十目标在无编译器/头文件/DKMS 的环境安装 GUI，实际 pkexec 成功/拒绝/退出清理、无显示诊断、
  受限 CPU、X11/Xwayland 窗口通过；随后安装驱动并验证重装/卸载。核心四环境各 18 个场景组通过。
- [逐目标结果、源码与产物哈希](validation/linux-integration-ci-88999aa.json)；20 份 DEB/RPM、源包与 SHA256SUMS 已保存在
  `dist/linux-integration-88999aa/`，下载/安装方式见 [产物说明](artifacts.md)。

普通用户运行 `octool`，在基础页明确申请硬件授权；`octool --diagnose` 无需显示服务器。
真实主板寄存器操作、交互认证窗口、Secure Boot/MOK、跨版本升级仍按[真机清单](hardware-acceptance.md)验收。
Rocky 的结果不自动等同于 Alma/RHEL；Windows/macOS 目前仅验证核心。
EL8 容器以本地账户测试，移除只在系统启动后可用的 NSS systemd 提供者；变更仅在可丢弃
容器内，前后内容已归档，安装包不修改用户的 NSS。目录账户认证仍需目标环境验收。

以下保留此前研究与验证记录。

更新日期：2026-10-09。接手时先读这里，再读根目录 `CHANGELOG.md`。

当前任务按作者最新澄清执行：**先重构OCTool整套组件，再把重构后的版本移植到其他系统**。
范围包括程序功能与界面、依赖组织、内核驱动、启动/安装脚本、polkit和说明；Windows版提供功能与接口参考。
当前主线是恢复功能规格、拆分业务逻辑与系统后端、实现可测试的新源码；后续用重构产物做跨系统适配和验收。
已有NVL/CPU等原指令证据继续用于实现与缺陷修正；原版私有运行库和旧加载器实验保留为对照资料。
先读[重构架构、组件职责与移植验收](multi-distro.md)，以及[功能恢复规格](platform-recovery.md)。
此前核心拆分增量 `beb10b7` 已把 PStates 和寄存器请求/校验/串行访问抽到[独立核心](../gui/core/hardware.h)，
Linux HAL 与 CPUID 亲和性处理进入[系统适配器](../gui/platform/linux_hwio.cpp)，Qt 界面复用核心。
云端四环境核心测试及 23/23 Linux 矩阵通过，见[实际结果与源码哈希](validation/refactor-hardware-ci-beb10b7.json)。
这仍是功能重构增量，Windows/macOS 的硬件后端及完整 GUI 尚未实现。
历史本机组件输入清单 `validation/component-porting-inputs-20261009.json` 来自原 Linux ZIP 与 Windows 目录的只读核对，未随此仓库发布。

本轮在用户提供的 `octool-linux-refactor.tar.gz` 上继续扩展。作者确认旧 GUI 源码丢失后，
授权按 Windows/Linux 二进制证据重构，首批选择基础信息和 MSR/MMIO/PCI 原始读写。
实际 Qt5 工程现已位于 `gui/`，96 字节 ABI 和旧 MMIO 协议保留；新 GUI 使用已有 HAL。
公开 GitHub Actions 的十目标基础版矩阵已有全绿记录，包含 AMD PStates 只读页；
这不代表旧 GUI 直接替换与硬件验收完成。最新复查和修正结果见下列报告。

- 本机历史报告 `project-research-2026-10-09.md`（未随仓库发布）：架构/分支/基线 CI，以及 Windows 解包、源包文件排除和本地验证记录。
- [2026-10-08 全面复查](review-2026-10-08.md)：基线证据、边界修正、未解决问题及后续研究顺序。
- [PCI/EC 协调与线程权限](bus-coordination.md)：接续修复、公共内核API选择、离线负例和本轮验证。
- [模块能力识别](module-capabilities.md)：只读查询、未知驱动拒绝扩展、旧模块MMIO对拍和升级检查。
- [采集完整性与映射生命周期](capture-integrity.md)：失效映射、fd 复用、错误/并发拒绝、v2 文件提交及无硬件门禁。

- [多发行版构建、运行和打包](multi-distro.md)：十目标、构建入口、依赖方案、CI 门禁。
- [已验证安装包和源包](artifacts.md)：产物名称、每目标选择方式、下载和哈希校验。
- [GitHub 仓库与 Actions](github-actions.md)：当前测试路径、公开状态与计费边界。
- [Actions 实测修复记录](actions-debugging.md)：软件源、Kbuild 探测与 EL headers 的实际失败及处理。
- [GUI 恢复与重构](gui-recovery.md)：源码丢失后的作者授权、Windows 输入现状与恢复边界。
- [首批 GUI 重构](gui-phase1.md)：作者选择的基础信息和原始读写、传输修复与测试范围。
- [平台面板恢复](platform-recovery.md)：23个原面板类、字段/操作规格、后端边界和重构实施顺序。
- [AMD PStates 只读页](amd-pstates.md)：CPU/能力探测、配置频率、原始值及旧算法核对边界。
- [EL8 静态 Qt SDK](qt-sdk.md)：成功 run、产物哈希、依赖配置及复用边界。
- [原版 ELF 的 EL8–EL10 兼容性](legacy-el-compatibility.md)：继续反汇编的调用点证据和私有运行库实验，区别于重构基础版。
- [原版启动权限与失败路径](legacy-el-privilege-analysis.md)：NASM 直接 syscall/端口、模块握手、MSR 错误处理与对拍入口限制。
- [原版平台判断与 Controls 分派](legacy-platform-dispatch.md)：PCI计数、DMI/VRM分支、hwloc及550个原指令决策实验。
- [原 Qt 回调索引](legacy-qt-callbacks.md)：39类451个元方法、原跳表/虚表及607项有界入口核对。
- [原 MSR 失败传播](legacy-msr-failures.md)：两个完整槽到文件接口、栈缓冲复用、148项有界实验和407处忙循环。
- [原 NVL 控件连接](legacy-ui-connections.md)：157个对象名、67项文字赋值、GT/NPU链及XOC双连接证据。
- [原主板与时序菜单](legacy-menu-routing.md)：W790/W890、TRX50/WRX90、客户端与多窗口分派的592项实验。
- [原AMD初始化](legacy-amd-initialization.md)：FamilyType不可达比较、Shimada来源、三套SMU软件表及385项实验。
- [原AMD命令传输](legacy-amd-transport.md)：固定BDF、端口等待耗尽仍发送、原libpci失败值和448项实验。
- [原AMD MP1与操作槽](legacy-amd-mp1.md)：固定身份读取、21次轮询、局部标志和失败后Applied的563项实验。
- [原Intel NGU与MMIO目标](legacy-intel-ngu.md)：两完整槽、读加基址/写直接传参、busy耗尽及266项原指令实验。
- [原内存页和64位写路由](legacy-mmio-clients.md)：原槽请求零地址、NVL查询仍写命令、fallback丢高32位及168项实验。
- [原公共邮箱与FIVR/FCH](legacy-mmio-services.md)：耗尽返回0、一致性AND比较、位操作边界及260项原指令实验。
- [原I2C与MMIO直接调用覆盖](legacy-mmio-i2c.md)：119项、错误返回/部分输出/无界等待，以及10函数28处CALL逐点请求核对。
- [原NVL配置、时钟及拓扑来源](legacy-nvl-profile.md)：96项完整存取、125项9595、130项配置拓扑、134项完整CPU拓扑及45项末段表实验；错误CPU归属、空表重建、邻表/计数覆盖，以及前序存取缺陷。
- 本机历史契约 `analysis/contracts/nvl-components.json`（未随仓库发布）：639 项子对象原指令实验、125 主描述/44 辅助描述；公开分析见上方 NVL 配置报告。
- [逆向覆盖台账](reverse-engineering-status.md)：清单、算法、模拟和真机证据的区别与尚未完成范围。
- [原版模块重复加载握手](legacy-module-handoff.md)：同名 `.ko` 的真实 EEXIST 路径、runner 探针与 Secure Boot 边界。
- [原版 MMIO 邮箱契约](legacy-mailbox-contract.md)：56 组原指令模拟、三份旧模块映射、错误完成字导致的兼容缺口。
- [原版 MMIO 保护层实验](legacy-mailbox-guard.md)：不改原调用函数的错误退出方案、原机器码原生执行与跨 EL 构建验证。
- [云端接入状态](cloud-access.md)：用户要求 OpenAI 托管云端；当前工具/CLI 的实际核实结果。
- [Linux Docker 执行指南](cloud-build.md)：独立 runner、日志和失败判定。
- [真机验收清单](hardware-acceptance.md)：安装、MOK、insmod/modprobe、MMIO 对拍、GUI。
- [验证状态及输入证据](verification-status.md)：本轮实际做过什么、哪些未做。
- [原始重构指南](../port/docs/refactor-guide.md)和[原始对拍指南](../port/docs/parity-verification.md)：历史结论及工具设计。
- [第一阶段分析](../analysis/docs/linux-porting.md)：原二进制与内核兼容性分析，保留原始验证范围。

## 项目约束

1. 版本使用点号，源码包名为 `octool-x.y.z-src.tar.gz`。
2. 持久知识放源码树 `docs/`，开发日志放根目录 `CHANGELOG.md`。历史日志保留在原路径。
3. 旧 GUI 二进制/调用点和 MMIO 线级协议不改；新 GUI 复用 HAL，不按内核版本号选择 API。
4. 不确定的硬件字段、面板含义、单位、缩放系数必须询问作者，不能从相似值推断规则。
5. 需要作者选择时，在回复末尾使用带选项的编号列表。

## 当前输入与剩余范围

- 已读取 `E:/Download/Edge/` 的两个 ZIP：Windows 包有 158 项但没有工程源码/PDB；
  Linux 包仍只有二进制，SHA-256 与先前输入相同。清单、哈希和元对象证据见
  [输入分析记录](validation/reference-packages-20260930.json)。原来 F 盘不可读的问题已解除。
- `gui/octool.pro`、实际 C++ 窗口/访问实现与 Qt 回归测试已接入 `port/gui/build.json`。
  新文件沿用 HAL 的 GPLv2；没有替旧二进制或附带第三方程序重新授权。
- 四组 CPU 已由作者确认，W790 更正为 w5-2565X、18 核，BIOS 均未知。
  AMD PStates 首批只读频率/原始值已实现；其余平台字段与写入规则继续核对。
- 当前本地执行环境为 Windows；用户现在授权使用公开 GitHub 仓库的 Actions Linux runner。
  真实结果以仓库 run 和验证文档为准，写好 workflow 不等于已经通过。
- 用户要求使用 OpenAI/Codex 托管云端，不是提供自有服务器。Cloud CLI 可连接服务，
  当前改用用户明确指定的 GitHub Actions 路径，不再以 Codex Cloud 环境作为测试前提。
- 当前基础版只依赖 QtBase Widgets/Concurrent，平台插件显式限定 xcb/offscreen。
  EL8 SDK 中其他模块的存在，不表示 GUI 已使用或验收这些功能。

## 范围和未决事项

CI 的 EL 三项默认使用 Rocky 对应大版本镜像。Alma/RHEL 使用相同构建入口，
但 Rocky 结果不是 Alma/RHEL 实测证据。RHEL 还需要已订阅的软件源，见真机清单。
当前只支持 x86_64/amd64，未提出 ARM 或 32 位支持。

MOK 签名解决“内核是否接受模块”。旧 GUI 的 `iopl`、`/dev/mem`、MSR 直接访问仍可能被
lockdown 限制；旧二进制没有被修改。新 GUI 原始访问走 HAL，但签名模块实际加载、权限和
硬件行为必须在真机验证，不能由容器签名/可见窗口推断 Secure Boot 下超频全功能通过。
