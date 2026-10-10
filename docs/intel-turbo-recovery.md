# Intel P/E 核睿频倍率分组

GUI 的 Intel Controls → Turbo ratio groups 与 CLI 的 `intel-turbo-read` / `intel-turbo-set`
共用独立的 `intel_turbo` 核心。当前只开放 GenuineIntel family 6/model B7；
E 核表还要求 CPUID.7.0:EDX[15] 宣告混合架构。逻辑 CPU 指定寄存器访问位置，
读取的是该处理器封装的分组限制，不是该逻辑 CPU 对应物理核心的专用倍率。

| 表 | 倍率寄存器 | 活动核心数量表 | 字段 |
|---|---|---|---|
| P 核 / primary | 1ADh | 1AEh | 8 对字节，低字节为 group 0 |
| E 核 / secondary | 650h | 651h | 8 对字节，低字节为 group 0 |

每次显式读取一个类型，共 8 行。核心数量来自配对表，不固定填 1..8，也不当作物理核心编号。
GUI 可复制快照，CLI 输出精确 64 位十六进制原值和逐组数据。
失败时不发布部分读到的表。读取成功但不满足编辑规则的表仍可查看，编辑被禁用。

## 修改规则

本轮只修改选中组的倍率，不改活动核心数量阈值。程序采用保守的 1..85 整数输入政策，
不是硬件全部编码范围或稳定性建议。数量为零的组不参与编辑；非零数量必须严格递增，
对应倍率必须为非零且随核心数量增加不升高。原表或拟提交的表不满足规则时拒绝修改。
这组局部规则不能认证最后一项覆盖了全部实际 SKU 核心，因此保留原阈值而不提供阈值设置。

设置需要读取到完整快照，且 `MSR_PLATFORM_INFO[28]` 允许编程、`MSR_FLEX_RATIO[20]` 未锁。
提交前重新检查身份、混合架构能力、两个权限寄存器、完整倍率表与核心数量表。
旧值发生变化时返回错误；相同倍率重新验证后不写入。
真正的修改只写一个倍率寄存器一次，保留其余七个字节。
随后完整读回上述上下文和 64 位倍率表；不接受固件忽略、其它位变化、取消或超时为成功。
失败不自动重试或回滚，GUI 清除旧编辑快照，CLI 保留写入是否尝试及完成次数。

```text
octool-cli intel-turbo-read --cpu N --core-type p
octool-cli intel-turbo-read --cpu N --core-type e
octool-cli intel-turbo-set --cpu N --core-type p --group 0..7 --value RATIO --apply
```

参数错误在打开设备前拒绝。`verified:true` 表示本次配置读回一致；
`hardware_effect_measured:false`，不代表实际频率、功耗或稳定性测量。
单进程事务锁不能阻止其它调参程序或内核驱动在读取与写入之间修改状态。

## 原程序与公开资料

- 固定原 ELF 的四个 writer 与 `get_32bit` helper：[静态指令](validation/legacy-intel-turbo-static.json)、
  [520 场景原指令实验](validation/legacy-intel-turbo-emulation.json)。输入逐字节截断，
  一次提交完整寄存器，不预读或读回；MSR wrapper 的返回值向上传递。没有执行真实硬件调用。
- 同一静态记录另含四个 reader；它们只是静态研究，没有计入上述模拟执行范围。
- [Intel SDM Volume 4，335592-085US](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf)
  §2.17.5、Table 2-46，印刷页 2-395/396：B7 适用的两个倍率表、八个字节、编程能力和倍率顺序要求。
  该版本在此表未列出 651h；不把后续 Core Ultra 表中的定义当成 B7 的独立认证。
- [Intel Raptor Lake-S FSP](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspsUpd.h)
  的 `TurboRatioLimitRatio/NumCore` 和 `AtomTurboRatioLimitRatio/NumCore` 各为 8 项，并明确成对使用。
  FSP 结构偏移不是寄存器地址；本轮数量表地址来自原程序独立读写函数。
- [Linux v6.12 turbostat](https://github.com/torvalds/linux/blob/v6.12/tools/power/x86/turbostat/turbostat.c)
  也读取 primary/secondary 两个倍率 MSR，但该版本的 ADL/RPL 配置不读取两个独立数量表，
  因而没有用其默认显示的 1..8 去替代原程序的数量表。
- [来源哈希清单](validation/intel-turbo-sources.json)。

新增核心测试覆盖两个类型、全部八组与允许输入、无效表、型号/能力/锁、相同值跳过、
旧值和阈值变化、各读取/CPUID 故障位置、写失败、完整读回逐位损坏、取消与超时。
GUI 与 CLI 均有对应验证。目标机上的固件行为尚未验收。
本功能不包括阈值编辑、逐物理核心 override、TVB、BCLK、VF 写入或 W790/W890 表。
