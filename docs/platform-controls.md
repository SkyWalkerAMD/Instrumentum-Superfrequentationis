# 平台功能恢复增量（2026-10-10）

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

本轮按作者要求继续恢复 AMD PStates、Intel Controls、AMD 调参、内存与主板功能。
本文件记录已经接入的代码及缺口，不代表原版 23 个平台类全部恢复，也不代表四台目标机器已通过验收。
编译与回归结果应以本分支对应的 Actions 为准，不能沿用旧基础版的成功记录。

后续源码增加 [AMD UMC 212 字段、明确地址选择及离线快照](amd-umc-recovery.md)，
以及 [CPUID 拓扑与 PStates 写入入口研究](amd-topology-recovery.md)。这些新增项的编译证据另行记录。

## 本轮已接入

| 页面/公共层 | 实现 | 当前边界 |
|---|---|---|
| AMD PStates | 一次事务采样；完整 9 位 VID 编码（bit32 合入）、IddValue/IddDiv 原始位 | 仍只读；只显示编码，不宣称 mV/A；不把 PStateEn 当硬件开关 |
| Intel Controls | 指定逻辑 CPU 的 RAPL PL1/PL2、使能、clamp、时间窗读取和保留位更新；HWP min/max/desired/EPP；DTS 温度 | RAPL 限明确型号，HWP 限 CPUID 能力并检查是否已启用、是否由 package 控制；不写锁位；没有 VF/OC mailbox、全平台 turbo/fabric/电压域 |
| AMD tuning / SMUIO | 三套互不混用的原 BIOS mailbox 地址、无写入 probe、选择命令/六参数、完整握手和固件返回；Shimada CCD/core/MHz 频率命令准备 | 仅 Shimada 开放已提取的控制命令；Phoenix/GPT 仅测试和版本查询；其它调参为原始编码，没有伪造物理单位/默认优化值 |
| Memory / Motherboard | DMI 主板和 BIOS、hwmon 温度/电压/电流/功耗/风扇/频率、驱动已绑定的 SPD；离线 SPD 文件 | 读取已有内核接口；不枚举探测裸 SMBus/EC、不装载驱动、不写 SPD；权限错误逐项显示 |
| SPD 解码 | DDR4/DDR5 基本类型、组织、料号/序列号、完整基础区 CRC、限定单片对称容量；修订版 1 的基础时序要求；DDR5 XMP 3.0 三个厂家档案的名称、电压、时序和逐区 CRC；EXPO 1.0 两组基础档案、独立启用及整块 CRC | DDR5 不足 512 字节时标注基础 CRC 未验证；不解码 3DS/混合容量；档案值不等于实际运行值；EXPO 增强时序/1.1 与用户档案尚未解码；[XMP](ddr5-xmp-recovery.md) / [EXPO](ddr5-expo-recovery.md) |
| 事务 | 同一 HardwareService 的整段独占、等待锁时计入截止时间、取消检查；多寄存器完整预检、旧值比较、保留位/锁位、失败立即停止 | 不是跨进程/内核驱动的硬件锁；不能中断已进入后端的系统调用；多写不具备原子回滚 |

这些对象构造、切换页签都不读取寄存器。只有明确点击读取/探测/发送才产生操作；关闭 Intel/AMD 页面会取消尚未提交的后续工作。
PStates 关闭后最多完成已开始的只读快照，不会发出设置写入。

## 平台与数据来源

Intel RAPL 白名单来自 Linux v6.12 `intel_rapl_common.c` 的 Core/SPR 包功耗布局，
型号编号核对 `arch/x86/include/asm/intel-family.h`。HWP 字段来自同版本 `msr-index.h`
与 `intel_pstate.c`。HWP 值为性能等级，不乘 100 冒充 MHz；PL/时间窗向下量化，读回需显式刷新。
所选逻辑 CPU 定位访问，RAPL 对应其 package；OS 的电源管理仍可能修改相同寄存器。

AMD 地址和命令来源为[初始化原指令证据](validation/legacy-amd-initialization-analysis.json)及
[传输证据](legacy-amd-transport.md)。Shimada 必须同时满足 AuthenticAMD、Family 1Ah、所选 PCI BDF
读取到 `1022:153a`；Phoenix 为 Family 19h 与 `1022:14e8`，GPT 为 Family 1Ah 与 `1022:1122`。
这只检查与原 profile 的身份匹配，不是固件版本/主板/CPU 商品型号的认证。
Shimada per-core 频率编码函数保留原 `amd_per_ccd_vermeer` / `per_ccx_freq_vermeer_fast` 的
CCD 高 4 位、core bits23:20、频率低 20 位；没有把 Linux CPU ID 当 CCD/core 编号。
页面上的 Prepare frequency 仅生成待审阅的命令参数，不访问硬件，也不确认 CCD/core 是否实际存在。
未确认单位的 PPT/TDC/EDC/FIT/VID 参数在界面中明确显示为 hex 编码，不提供 mV/A 解释。

新握手先等上次响应非零，再清响应、写六参数、提交一次消息，等非零响应。
只有响应 1 且全部参数读取成功才算返回成功；其它固件状态保留原值。
任一步失败即返回，超时/传输断开不重发，不复制原程序的 Applied 无条件提示、TSC 修改或伪端口锁。
F8/FC 间接访问仍是原程序 BIOS SMU 通道，不混同其它工具的 60/64 SMN 或 RSMU/MP1 地址。

SPD 基本偏移参考 coreboot DDR4/DDR5 头文件。随后用 Advantech 内存条数据表和
[illumos DDR5 字段定义](https://github.com/illumos/illumos-gate/blob/master/usr/src/lib/libjedec/common/spd_ddr5.h)
交叉核对：DDR5 byte 6 的 I/O width 在 bits7:5；不能沿用所缓存 coreboot spd_bin.c 的低 3 位算法。
厂商 x8 样本 byte6=0x20 是此次修正的回归依据。DDR5 CRC 覆盖 bytes0..509；DDR4 覆盖 bytes0..125。
DDR4 的有符号细调和两代基础时序字段按相应 SPD 布局转换，界面明确显示 ps/ns，不当作当前训练值。
来源版本与 SHA-256 见[来源记录](validation/platform-control-sources.json)。
hwmon 单位按[内核 ABI 文档](https://www.kernel.org/doc/html/latest/hwmon/sysfs-interface.html)。
DDR5 EEPROM 由 [spd5118](https://www.kernel.org/doc/html/latest/hwmon/spd5118.html) 提供；旧内核未暴露时支持导入 dump。
系统有驱动不等于驱动支持这块主板，读取失败不填零或沿用旧值。

## 尚未完成的原功能

- PStates 电压/电流的型号专用换算和全核一致写入规则；已知旧版电压/电流算法不能直接作为真值。
- Intel VF/OC voltage、逐核 ratio / VID rank / SP、W790/W890 各电压域、fabric/BCLK。
- AMD 自动 CCX/core 拓扑、物理单位调参、VF/boost curve、profiles/hotkeys、PM 表解码及目标固件验证。
- Intel/AMD 运行时内存时序、训练设置、DDR5 PMIC、主板 EC/VRM/时钟芯片及相关写入。

以上继续需要从原函数消费者和平台资料建立字段绑定，不能用通用 raw 页或新增页签来宣称这些功能完成。

## 验证设计

独立核心新增 8 组场景：完整预检与保留位、部分失败、并发/截止/取消、RAPL 编码/锁/旧快照、
HWP 边界/错误平台零 MSR、SMU 顺序/拒绝/短路、SMU 超时、SPD 长度/CRC/位32。
Qt 回归增加构造零访问、未知 Intel 型号零 MSR、无效 AMD 输入零访问、sysfs fixture 单位/故障/SPD。
另用厂商公开字段构造固定 CRC 向量，验证 DDR5 x8/容量/基础时序、损坏与截断 CRC、
DDR4 有符号细调；这些派生向量的无关字节为零，不冒充实机 SPD 采集。
这些都是合成设备测试，没有对目标真机执行超频或寄存器写入。

## 本轮验证与交付

最终生产代码 `0b514bbf663f14bdefe684b4e58ff1c5a298b6ea`，核心 [4/4](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068831)、Linux [23/23](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068664) 全部通过。
十个 Linux 目标各 21 项 Qt 与 29 项 Python 回归、11 套实际目标内核在 VM 中启动；
核心四环境各 4 个 CTest、26 个场景组。Windows 本机的 45 项回归中 36 项通过、9 项因缺少
C++/Linux 环境跳过，对应编译及系统检查由云端执行。
20 份安装包和源包保存在 `dist/platform-recovery-0b514bb/`。
[完整报告](validation/platform-recovery-ci-0b514bb.json)记录源码一致性、包/截图/模块哈希及验收限制。
