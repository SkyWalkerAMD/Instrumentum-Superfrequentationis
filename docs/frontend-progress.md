# GUI / CLI 同步进度

2026-10-12 本轮新增 [TVB 温度与降倍率分档查询](intel-tvb-table.md)：GUI `TVB thresholds` 与 CLI `intel-tvb-table` 共用核心。限定 B7 裸机；分档写入仍待恢复。生产提交 `aca1652` 的 39 项云端任务全部通过，见 [验证交付](intel-tvb-table-validation.md)。

2026-10-12 早先恢复 [TVB 两项配置开关](intel-tvb.md)：GUI `TVB configuration`、CLI `intel-tvb-read/set` 共用核心。限定 06_B7 裸机；温度分档读取现已由本页首项补充。生产提交 `743c40f` 的 39 项云端任务全部通过，见 [验证交付](intel-tvb-validation.md)。

GUI `Intel Controls → Clock information` 与 CLI `intel-clock-read` / `intel-clock-decode`
同步提供 [Intel BCLK 与最后一次 MRC 请求](intel-clock.md)，限定 B7/A700。历史训练请求不代表实时内存频率。

GUI Intel Controls / Memory timings 与 CLI `intel-memory-read`、`intel-memory-decode`
同步提供 [Raptor Lake-S 内存控制器时序与快照](intel-memory.md)，限定 B7/A700。
2026-10-12 当前扩展为 57 字段，含 [RTL 与掉电参数](intel-memory-extended.md)及
[刷新、自刷新与 ZQ 参数](intel-memory-refresh.md)，兼容旧 v1/v2 快照。

新增 [Intel 中间组阈值编辑](turbo-thresholds.md)和 [DDR5 混合 rank 容量](spd-asymmetric.md)，两端共用实现与约束。

继续补齐 [DDR4 非对称容量](ddr4-asymmetric.md)：SDP/DDP 的 Rank 分配与 3DS 层数单独处理，GUI/CLI 同步显示分项。

2026-10-11：新增 [AMD PM 遥测与快照互通](amd-pm-recovery.md)。GUI / CLI 同步解码八个精确版本的
24 项公共字段，并可读取系统已有兼容驱动的 sysfs 表；未新增原生传输、目标映射或调参写入。

两个入口共用 `gui/core`，Linux 硬件访问共用 HAL 与驱动。新增功能同时落到 GUI 页面、CLI 命令、
共同核心和两套入口测试；包以同一个 Git 提交标识交付。当前还原范围如下。

| 功能 | GUI | CLI | 共同边界 |
|---|---|---|---|
| AMD PStates | AMD PStates 页 | `amd-pstates` | Family 1Ah 定义读取，未恢复设置 |
| Intel RAPL / HWP / DTS | Power / performance | `intel-read` / `intel-set` | 型号/能力门控；含 HWP 活动窗口，13 项设置完整回读 |
| Intel core/cache offset、最大倍率、目标电压与模式 | Core / cache voltage and ratio | `intel-oc-read` / `intel-oc-set` | B7；[目标电压 + Adaptive/Override](intel-voltage-recovery.md)，锁、旧值与完整回读 |
| Intel V/F 点 | V/F points 查询、准备与设置 | `intel-vf-read` / `intel-vf-set` | B7，两域，候选 1..15 查询及单点 offset；[完整上下文核对](intel-vf-write-recovery.md)，不改变 override 模式 |
| Intel 睿频分组 | Turbo ratio groups | `intel-turbo-read` / `intel-turbo-set` | B7；P/E 两表，读取活动核心阈值，单组倍率或中间组阈值设置与完整读回；末组阈值保持 |
| Intel Ring / LLC 范围 | Ring / LLC range | `intel-uncore-read` / `intel-uncore-set` | B7/8F；最小与最大倍率同时设置，保留其它位、旧值检查及完整读回；[实现说明](intel-uncore-recovery.md) |
| Intel 内存控制器 | Memory timings | `intel-memory-read/decode` | B7/A700；57 字段含 RTL、掉电、刷新、自刷新和 ZQ，v1/v2/v3 双向快照；仅读取 |
| Intel 时钟信息 | Clock information | `intel-clock-read/decode` | B7/A700；报告 BCLK kHz，历史 MRC Gear/倍率/VDDQ 请求；独立只读快照，不推导实时内存频率 |
| Intel TVB 两项配置 | TVB configuration | `intel-tvb-read/set` | 06_B7 裸机；高温倍频裁剪与电压优化开关、锁/旧值检查、保留其它位、完整回读；分档查询见下行 |
| Intel TVB 分档查询 | TVB thresholds | `intel-tvb-table` | 06_B7 裸机；两组温度 / 降倍率、单项或八项候选、逐项错误及原值；未开放分档设置或逐物理核心表 |
| AMD SMU | AMD tuning | `amd-smu-probe/read/send` | 现有身份/命令白名单与原始参数 |
| AMD 曲线读取 | AMD tuning 曲线查询 | `amd-curve-read` | Shimada；显式固件 CCD/core；不推导 mV |
| AMD 拓扑 | AMD tuning | `amd-topology` | CPUID 拓扑，不作为固件目标映射 |
| AMD PM 遥测 | AMD tuning → PM telemetry | `amd-pm-read/decode` | 八个精确 Shimada 版本、24 项公共字段；已有驱动读取、原始文件解码与 JSON 双向导入；BCLK 修正需外部值，未完成整表与物理目标认证 |
| AMD UMC | AMD UMC，打开/比较快照与差异筛选 | `amd-umc-read/decode/diff` | 212 字段、56 原始寄存器；GUI/CLI 双向离线导入与共用比较；无时序写入 |
| DMI / 传感器 / SPD | Memory / Motherboard | `inventory` / `spd-decode` | 读取现有系统节点与离线 SPD；[DDR4 XMP 2.0](ddr4-xmp-recovery.md) 两组档案、电压/时序/独立时间基准（无扩展 CRC）；[DDR5 XMP 3.0](ddr5-xmp-recovery.md) 三个厂家档案含 8 项次级时序和独立周期下限（ps / nCK）；[EXPO 1.0](ddr5-expo-recovery.md) 两组基础档案、电压/时序和逐区 CRC；[XMP 用户槽读取与 EXPO 增强时序存储字](spd-user-enhanced.md)，启用状态未知 |
| 原始 MSR / MMIO / PCI | Register 编辑页 | `register-read` / `register-write` | 相同宽度、范围与对齐校验；明确目标，写入不自动回读 |

GUI 使用图形授权，CLI 使用终端权限。UMC 的 GUI/CLI 快照和 CLI decode 报告可双向导入，
两端支持同 bank/slot 的离线比较，区分字段变化、原始字变化与数据缺失。
[UMC 离线说明](umc-offline.md)记录格式、文件边界和比较语义。

SPD 两端还同步支持[基础 CAS 位图及 XMP 3.0 CAS / 命令率](spd-cas-recovery.md)，
区分零位图、未知修订版和未知代码，保留原始字节；不代表当前训练配置。
现也支持[DDR4 写延迟与 DDR5 基础次级时序](spd-base-timings.md)，DDR5 的时间和周期下限分别显示，
版本 1.2 新增的三对字段按基础修订版判断。
另已接通[DDR4 XMP CAS/推荐 DIMM 数及堆叠组织](spd-profile-geometry.md)，修正 DDR5 四/八子通道容量，
支持对称堆叠、DDR5 混合 rank 分项容量与 3DS 不同逻辑 rank 刷新下限；基础区和模块公共区分别检查修订版。

双方共同未完成：AMD PStates 写入、曲线设置与物理目标映射、完整 PBO 高层限制、
Intel 其它型号的 VF 及 W790/W890 电压/逐核/fabric、其它 Intel 型号 RTL、剩余内存时序/IOL 与训练控制、板级 PMIC/VRM/EC/时钟写入。
四台目标真机验收与 Secure Boot 实机签名部署也未完成。详见[还原状态](recovery-status.md)。
