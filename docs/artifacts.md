# 安装包与源码归档

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
2026-10-10 无界面交付：`dist/headless-cli-b2ef0a3/`，生产代码 `b2ef0a3cebd44c6bceb6b4e8282deaed4bd03c4d`。

- 每个目标子目录提供一个 `octool-cli` 包和一个可选 `octool-hwio-dkms` 包；合计 20 包。
- `octool-2.0.1-src.tar.gz`、对应 `.sha256`、`SHA256SUMS` 与安装包一并保存。
- CLI 包无需图形库，体积 138,156–173,404 字节；安装后的可执行文件 324,256–380,240 字节。
- [CLI 工作流](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38016466064) 12/12，[现有 Linux 回归](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38016466047) 23/23。
- 安装、命令、退出码和限制见[无界面说明](headless-cli.md)，逐目标字节数见[本轮验证](headless-cli-validation.md)。

版本号仍为研究阶段 `2.0.1`，以提交目录和校验表区分产物。CLI 包可独立安装；DKMS 包需按目标内核安装。
此目录不包含 GUI 包，已有 GUI 交付仍在下方历史目录。

以下保留此前记录。

<!-- amd-curve-recovery-06b477c -->
最新生产代码 `06b477c` 已验证：[核心 4/4](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38013841095)、[Linux 23/23](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38013841147)。
新增 [Shimada 曲线原值查询](amd-curve-query-recovery.md)，输入为显式固件编号，失败结果不会显示为有效值。
十个 x86_64 Linux 目标各 32 Qt / 32 Python；四个核心环境各 7 CTest / 56 场景组；65 分析测试、11 套目标内核 VM。
安装包位于 `dist/amd-curve-recovery-06b477c/`：每目标 GUI + DKMS，共 20 个 DEB/RPM，另有源码包、侧车 SHA 和 `SHA256SUMS`。
[完整证据](validation/amd-curve-recovery-ci-06b477c.json)包含产物哈希、源码比对、原指令实验及 200 张截图。
曲线设置、物理核心映射和四台目标机器的固件行为仍未验收；[剩余功能](recovery-status.md)另列。

以下保留此前验证记录。

<!-- intel-ratio-recovery-fa39af6 -->
最新生产代码 `fa39af6` 已验证：[核心 4/4](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38011778227)、[Linux 23/23](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38011778246)。
新增 [core/cache 最大 OC 倍频](intel-ratio-recovery.md)，两种设置均保留其它字段并完整回读。
十个 x86_64 Linux 目标每个 29 Qt / 31 Python，各核心环境 6 CTest / 49 场景组，11 套真实目标内核 VM 启动。
安装包保存于 `dist/intel-ratio-recovery-fa39af6/`：每目标 GUI + DKMS，共 20 个 DEB/RPM；另有源码包、侧车 SHA 和 `SHA256SUMS`。
[完整证据](validation/intel-ratio-recovery-ci-fa39af6.json)包含产物哈希、源码比对、原指令观测和 200 张界面截图。
目标物理机器的固件行为、Secure Boot 和实际电压/频率效果未验收；[剩余功能](recovery-status.md)仍独立列出。

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

| 安装目标 | 子目录 | GUI 包大小 | DKMS 包大小 |
|---|---|---|---|
| el8 | `el8/` | 9.93 MiB | 18.6 KiB |
| el9 | `el9/` | 9.66 MiB | 18.6 KiB |
| el10 | `el10/` | 9.66 MiB | 18.7 KiB |
| ubuntu20.04 | `ubuntu20.04/` | 13.08 MiB | 10.4 KiB |
| ubuntu22.04 | `ubuntu22.04/` | 13.08 MiB | 10.4 KiB |
| ubuntu24.04 | `ubuntu24.04/` | 13.08 MiB | 10.4 KiB |
| ubuntu26.04 | `ubuntu26.04/` | 13.08 MiB | 10.4 KiB |
| debian11 | `debian11/` | 13.08 MiB | 10.4 KiB |
| debian12 | `debian12/` | 13.08 MiB | 10.4 KiB |
| debian13 | `debian13/` | 13.08 MiB | 10.4 KiB |

以上均为 x86_64，版本仍为未发布 2.0.1；按提交目录区分构建。每目标独立提供 GUI 与 DKMS 包。
安装时 DKMS 为当前匹配内核生成 `.ko`，不可跨内核直接混用验证模块。
本次核心/UI 源码的 23 个变更文件已与源包及用户原工作目录逐一核对；
用户原工作目录内额外研究和 CI 文件保留，不宣称整个工作目录等同于该提交。


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

安装目标：Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、Rocky Linux 8/9/10，均为 x86_64。
每目标子目录各含 GUI 包和 DKMS 包；内核版本见逐项报告。GUI 可以单独安装。
`.ko` 仍由 DKMS 为用户实际内核构建，不直接跨内核复制验证用模块。

以下保留此前记录；本次新增功能与验证以以上链接为准。


最新平台恢复构建为 `0b514bb` / [Actions](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068664)，23/23 成功。
20 份目标 DEB/RPM、源码归档和 SHA256SUMS 已保存到 `dist/platform-recovery-0b514bb/`。
版本仍为未发布 2.0.1，用提交目录区分构建；请从对应系统的子目录取包。
[完整证据](validation/platform-recovery-ci-0b514bb.json)保存测试、授权、内核启动和 160 张截图的哈希。
[功能范围](platform-controls.md)包含明确剩余缺口，不能把安装成功等同于原版全功能或真机调参通过。

| 目标 | 本轮 VM 实际内核 | 子目录 |
|---|---|---|
| el8 | `4.18.0-553.el8_10.x86_64` | `el8/` |
| el9 | `5.14.0-687.56.1.el9_8.x86_64` | `el9/` |
| el10 | `6.12.0-211.64.1.el10_2.x86_64` | `el10/` |
| ubuntu20.04 | `5.4.0-216-generic` | `ubuntu20.04/` |
| ubuntu22.04 | `5.15.0-198-generic`, `6.8.0-138-generic` | `ubuntu22.04/` |
| ubuntu24.04 | `6.8.0-146-generic` | `ubuntu24.04/` |
| ubuntu26.04 | `7.0.0-38-generic` | `ubuntu26.04/` |
| debian11 | `5.10.0-46-amd64` | `debian11/` |
| debian12 | `6.1.0-53-amd64` | `debian12/` |
| debian13 | `6.12.111+deb13-amd64` | `debian13/` |

本轮 Qt 回归为每目标 21 项，Python 为每目标 29 项；独立核心四环境各 26 个场景组。
这些内核模块是验证产物；安装时仍通过 DKMS 为用户当前匹配内核构建，不能跨内核直接混用 `.ko`。
运行 GUI 可独立安装，授权、运行库及驱动依赖方式见[Linux 适配文档](multi-distro.md)。

以下为此前构建记录，包和源代码哈希不可混用。

最新完整验证为 `88999aa` / [Actions](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37923477646)，23/23 成功。
20 份目标系统 DEB/RPM 与经验证源包保存到 `dist/linux-integration-88999aa/`，根目录 `SHA256SUMS`
逐项记录安装包哈希；源包另有同名 `.sha256`。版本仍为未发布的 2.0.1，用提交目录区分各轮构建。
[完整证据](validation/linux-integration-ci-88999aa.json)包含全部 11 套 VM 内核、授权报告、100 张窗口截图和源代码一致性记录。

| 目标系统 | 已启动并验证模块的内核 | 安装包目录 |
|---|---|---|
| Rocky Linux 8 | `4.18.0-553.el8_10.x86_64` | `el8/` |
| Rocky Linux 9 | `5.14.0-687.56.1.el9_8.x86_64` | `el9/` |
| Rocky Linux 10 | `6.12.0-211.62.1.el10_2.x86_64` | `el10/` |
| Ubuntu 20.04 | `5.4.0-216-generic` | `ubuntu20.04/` |
| Ubuntu 22.04 | `5.15.0-198-generic`, `6.8.0-138-generic` | `ubuntu22.04/` |
| Ubuntu 24.04 | `6.8.0-146-generic` | `ubuntu24.04/` |
| Ubuntu 26.04 | `7.0.0-38-generic` | `ubuntu26.04/` |
| Debian 11 | `5.10.0-46-amd64` | `debian11/` |
| Debian 12 | `6.1.0-53-amd64` | `debian12/` |
| Debian 13 | `6.12.111+deb13-amd64` | `debian13/` |

该表是本次实际版本记录；用户机器通过 DKMS 为自己的匹配内核重新编译，不能直接混用这些 `.ko`。

GUI 包包含独立硬件辅助程序和 polkit 策略，DKMS 为可选推荐依赖。
例如在 Debian12 对应目录，只装界面使用 `sudo apt-get install --no-install-recommends ./octool-2.0.1-1.amd64.deb`；
需要驱动时，按[安装清单](hardware-acceptance.md)先准备匹配头文件再安装同目录 DKMS 包。
运行 `octool` 后在基础页申请授权；无显示诊断使用 `octool --diagnose`。

```sh
gh run download 37923477646 --repo SkyWalkerAMD/Instrumentum-Superfrequentationis \
  --pattern 'desktop-*' --pattern 'kernel-*' --name octool-source --dir build/actions/37923477646
```

下面是此前构建的历史记录，文件哈希不可混用。

更新：2026-10-08。当前功能范围为基础信息、原始 MSR/MMIO/PCI、AMD PStates 只读频率/原始值。
原 OCTool 的全部 Intel/AMD 调参面板尚未恢复，实机验收见 [清单](hardware-acceptance.md)。

## 完整通过的构建

后续研究代码`fa4c541`已完成[run37766384139](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37766384139)
23/23验证；原指令2501项、GUI/包与模块范围详见[证据](validation/legacy-initialization-ci-fa4c541.json)。
该轮包在对应Actions artifact中，未把其文件哈希混用于下述已保存的3920018包。

已完整下载保存的安装包来自[3920018 / run37749651590](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37749651590)，
23/23成功，包含采集生命周期/v2完整提交、每目标46项合成采集和21项Python，以及原有GUI/模块门禁。
十目标20份安装包保存到`dist/packages-3920018/<目标>/`，附`SHA256SUMS`；名称仍沿用2.0.1。
[最新文件哈希记录](validation/deliverables-3920018.json)包含11套kernel release、包SHA与100张截图SHA；
[Actions记录](validation/capture-integrity-3920018.json)另存artifact ZIP digest，二者不混用。
原始下载目录为`build/actions/37749651590/`。本轮未改GUI/HAL/kmod生产代码，GUI包内的docs会更新。

之前[f2142e4 / run37741335600](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37741335600)
的PCI/EC、线程权限、模块能力查询结果及`dist/packages-f2142e4/`仍保留，
见[先前交付记录](validation/deliverables-f2142e4.json)，勿混淆不同构建的文件哈希。

以下是历史构建，不能用它的旧包验证新能力查询：
[run 36670288030](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36670288030)
（c48a38f）有 23 个 job 全部成功。十目标各生成一个 GUI 包与一个 DKMS 包，共 20 份。
[完整证据](validation/actions-run-36670288030.json)记录包名、SHA-256、大小、镜像 digest、
11 个 kernel release、日志与截图哈希。文件哈希与 GitHub artifact ZIP digest 是不同对象，不混用。

| 目标目录 | GUI 包 | 模块包 |
|---|---|---|
| el8 | octool-2.0.1-1.el8.x86_64.rpm | octool-hwio-dkms-2.0.1-1.el8.noarch.rpm |
| el9 | octool-2.0.1-1.el9.x86_64.rpm | octool-hwio-dkms-2.0.1-1.el9.noarch.rpm |
| el10 | octool-2.0.1-1.el10.x86_64.rpm | octool-hwio-dkms-2.0.1-1.el10.noarch.rpm |
| ubuntu20.04 / ubuntu22.04 / ubuntu24.04 / ubuntu26.04 | octool-2.0.1-1.amd64.deb | octool-hwio-dkms-2.0.1-1.amd64.deb |
| debian11 / debian12 / debian13 | octool-2.0.1-1.amd64.deb | octool-hwio-dkms-2.0.1-1.amd64.deb |

deb 文件名相同，但依赖由各目标 dpkg-shlibdeps 分析生成，必须从对应目录选择。
发行 GUI 均来自 EL8 基线；每目标另编的 native GUI 仅用于回归，不替代发行包的基线程序。
RPM 运行包可能由 rpmbuild strip，不能把安装包内 ELF 哈希直接当作未 strip 的 native ELF 哈希。

## 取得产物

Actions artifacts 保留 7 天，需及时保存。每个 `desktop-<目标>` 的 `packages/` 含两份安装包，
`kernel-<目标>` 含编译日志、原始和临时测试签名模块。不要将 CI 测试证书当作本机可信 MOK。

```sh
gh run download 37749651590 --repo SkyWalkerAMD/Instrumentum-Superfrequentationis \
  --pattern 'desktop-*' --pattern 'kernel-*' --pattern 'octool-source' --dir build/actions/37749651590
```

Windows工作副本的`dist/packages-3920018/<目标>/`是最新验证副本；旧目录保留历史版本。
后续若仅更新文档并重新构建，新的包/源码哈希也可能改变，必须按实际run核对，不能混用记录。

Linux 上安装（先满足当前内核头文件和 DKMS 依赖，完整步骤见真机清单）：

```sh
# 举例：Debian12，当前目录是源代码根目录
cd dist/packages-3920018/debian12
sudo apt-get install ./octool-hwio-dkms-2.0.1-1.amd64.deb ./octool-2.0.1-1.amd64.deb
# EL 对应目录使用 sudo dnf install ./octool-hwio-dkms-*.rpm ./octool-2.0.1-*.rpm
```

## 源码

`octool-source` artifact 只在总门禁成功后生成，名称为 `octool-2.0.1-src.tar.gz`，有同名 `.sha256`。
本轮cloud源包存于`dist/cloud-3920018/`，SHA为`1e149d3d85602456a96c2c32aa93be7a55c66166d1a33c6634aaf4938dd184ea`。
它精确对应已验证3920018；下列命令生成的`dist/octool-2.0.1-src.tar.gz`另含最新文档/证据归档。
本地重新归档当前源码及最新文档：

```sh
python3 port/tools/make_source.py --require-gui
sha256sum -c dist/octool-2.0.1-src.tar.gz.sha256
```

源包包括 gui/、port/、analysis/、docs/、根 CHANGELOG 和 Actions workflow，不含旧预编译二进制、
build/dist、编译对象、模块或私钥。可选二进制分析工具的依赖见 analysis/tools/requirements-reference.txt，
普通编译不需要这些分析依赖。
