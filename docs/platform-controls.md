# 平台功能恢复增量（2026-10-09）

本轮按作者要求继续恢复 AMD PStates、Intel Controls、AMD 调参、内存与主板功能。
本文件记录已经接入的代码及缺口，不代表原版 23 个平台类全部恢复，也不代表四台目标机器已通过验收。
编译与回归结果应以本分支对应的 Actions 为准，不能沿用旧基础版的成功记录。

## 本轮已接入

| 页面/公共层 | 实现 | 当前边界 |
|---|---|---|
| AMD PStates | 一次事务采样；完整 9 位 VID 编码（bit32 合入）、IddValue/IddDiv 原始位 | 仍只读；只显示编码，不宣称 mV/A；不把 PStateEn 当硬件开关 |
| Intel Controls | 指定逻辑 CPU 的 RAPL PL1/PL2、使能、clamp、时间窗读取和保留位更新；HWP min/max/desired/EPP；DTS 温度 | RAPL 限明确型号，HWP 限 CPUID 能力并检查是否已启用、是否由 package 控制；不写锁位；没有 VF/OC mailbox、全平台 turbo/fabric/电压域 |
| AMD tuning / SMUIO | 三套互不混用的原 BIOS mailbox 地址、无写入 probe、选择命令/六参数、完整握手和固件返回 | 仅 Shimada 开放已提取的控制命令；Phoenix/GPT 仅测试和版本查询；调参为原始编码，没有伪造物理单位/默认优化值 |
| Memory / Motherboard | DMI 主板和 BIOS、hwmon 温度/电压/电流/功耗/风扇/频率、驱动已绑定的 SPD；离线 SPD 文件 | 读取已有内核接口；不枚举探测裸 SMBus/EC、不装载驱动、不写 SPD；权限错误逐项显示 |
| SPD 解码 | DDR4/DDR5 基本类型、组织、料号/序列号；DDR4 基础块 CRC、限定单片对称 DDR4 容量 | DDR5 CRC 和当前训练时序未恢复；SPD 信息不等于实际运行时序 |
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
未确认单位的 PPT/TDC/EDC/FIT/VID 参数在界面中明确显示为 hex 编码，不提供 mV/A 解释。

新握手先等上次响应非零，再清响应、写六参数、提交一次消息，等非零响应。
只有响应 1 且全部参数读取成功才算返回成功；其它固件状态保留原值。
任一步失败即返回，超时/传输断开不重发，不复制原程序的 Applied 无条件提示、TSC 修改或伪端口锁。
F8/FC 间接访问仍是原程序 BIOS SMU 通道，不混同其它工具的 60/64 SMN 或 RSMU/MP1 地址。

SPD 位定义参考 coreboot `src/lib/spd_bin.c`、`device/dram/ddr4.c` 与 DDR4/DDR5 头文件；
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
这些都是合成设备测试，没有对目标真机执行超频或寄存器写入。
