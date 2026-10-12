# 功能还原状态

<!-- intel-ring-downbin-pending -->
2026-10-12：GUI / CLI 同步新增 [Intel Ring DownBin](intel-ring-downbin.md)，限定 B7 裸机；原版 Ring / Vmax 指令已隔离仿真，Vmax 仍未开放设置。云端验证进行中。以下保留上一批已验证记录。
<!-- /intel-ring-downbin-pending -->

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
2026-10-10 无界面入口已增加：`octool-cli`，验证源码 `b2ef0a3`。
它复用本页已有的核心功能边界，提供 SSH/终端操作和 JSON 输出，不新增未还原的硬件语义。
主板/传感器/SPD 读取移入 GUI、CLI 共用的无 Qt 实现。
[使用说明](headless-cli.md)、[验证记录](headless-cli-validation.md)：十个发行版各 9 CTest，独立 CLI 安装/运行/卸载重装通过，既有 Linux 23/23 回归通过。
PStates 写入、曲线写入、Intel server VF/fabric、板级专用控制等缺口及四台真机验收仍保留。

以下保留此前记录。

`06b477c` 是此前 [Shimada 原始曲线查询](amd-curve-query-recovery.md)的历史版本，
[当时验证](validation/amd-curve-recovery-ci-06b477c.json)为核心 4/4、Linux 23/23。
最新生产版本以本页顶部为准，下表按最新功能范围汇总。
原版全部功能仍未完成。安装兼容、自动化回归、原指令研究和真实硬件验收分别记录，
没有能代表四大模块全部功能的可靠百分比；212 个 UMC 字段也不是整个软件的功能分母。

| 用户要求的模块 | 已接入可编译程序 | 尚未还原的部分 |
|---|---|---|
| AMD PStates | 按逻辑 CPU 采样 P0–P7、上限/能力检查、频率、64 位原值、完整 9 位 VID / Idd 编码 | 型号专用 mV/A 换算；满足跨核/跨 coherent fabric 一致性的设置 |
| Intel Controls | RAPL、HWP 含活动窗口，13 项设置完整读回；温度；Raptor Lake-S core/cache offset、最大 OC ratio、目标电压与 Adaptive/Override；VF 点查询及受条件限制的单点 offset；P/E 睿频分组读取、单组倍率及中间组活动核心阈值设置；B7/8F Ring/LLC 最小、最大倍率范围；B7/A700 报告 BCLK 与历史 MRC 请求；B7 TVB 高温倍频裁剪与电压优化开关、活动 P 核候选表的两组温度 / 降倍率查询及保守范围单字段设置 | W790/W890 电压域；其它型号 VF、逐物理核心 override、末组阈值/组数量重建、TVB 完整写入范围 / 逐物理核心表与其它型号控制、VID rank/SP、fabric/BCLK 写入和其它型号时钟信息 |
| AMD 调参 | 三套独立 BIOS SMUIO；Shimada 已提取命令和逐核频率参数准备；CPUID 稀疏拓扑；限定身份的原始曲线查询；PM 表八个版本的 24 项公共字段、已有驱动读取及 GUI/CLI 快照互通 | CPUID 与固件目标 ID 对应；MP1/PBO 高层单位、曲线设置/电流/温度限制、完整 PM 表与原生传输、profiles/hotkeys |
| 内存与主板 | DMI/BIOS、内核传感器、绑定驱动的 SPD、DDR4/5 基础 CRC/组织/时序/CAS 支持列表，含 DDR4 写延迟、DDR5 次级时序及 1.2 新增字段；DDR4 XMP 2.0 两组电压/时序、独立时间基准、限定 CAS/推荐 DIMM 数及无扩展 CRC 状态；对称堆叠容量、DDR4 已确认布局及 DDR5 混合 rank 分项、DDR5 子通道/3DS 刷新要求；DDR5 XMP 3.0 厂家档案名称、电压、基础与 8 项次级时序/独立周期下限、CAS/命令率及逐区 CRC；EXPO 1.0 两组基础档案、增强时序存储字及整块 CRC；XMP 3.0 两个用户槽独立检查/校验与解码；AMD UMC 212 字段、GUI/CLI 快照双向离线导入及同 bank/slot 比较；限定 B7/A700 的 Intel 内存控制器 57 字段（含有效 rank RTL、掉电、刷新、自刷新和 ZQ 参数）、双次一致读与 GUI/CLI v1/v2/v3 快照 | EXPO 增强启用语义 / 1.1 / 用户档案；XMP 用户槽启用与写入；XMP 3.0 推荐 DIMM 数；其余 DDR4 非对称 Rank 布局；UMC 各型号物理单位/通道标签；其它 Intel 型号 RTL、剩余时序/IOL、实时内存频率及特殊 Gear 换算；PMIC/VRM/EC/时钟芯片和训练写入 |

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
