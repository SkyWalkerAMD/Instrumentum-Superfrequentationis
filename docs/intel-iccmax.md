# Intel IA ICCmax：限定 Alder Lake 的电流上限

GUI `Intel Controls → IA current limit` 与 CLI `intel-iccmax-read/set` 共用核心。
本功能读取 IA 供电轨的配置电流上限，并在保守门控条件满足时设置一个精确值。
IA 轨由 P 核、E 核和 Ring 共用；逻辑 CPU 用于定位邮箱，不代表逐核设置。

## 型号和输入

只接受 GenuineIntel family 6 的以下型号 / stepping，并要求裸机和 CPUID MSR 能力：

| Model | Stepping |
|---|---|
| 97h（Alder Lake-S） | 0、1、2、4、5 |
| 9Ah（Alder Lake-H/P） | 1、2、3、4 |

不是以代号或主板名称猜测全部处理器支持。B7、其它 stepping、GT/SA、W790/W890 和
其它服务器电流限制仍未接入。原版其它 ICCmax 路径的[历史研究](intel-iccmax-research.md)继续保留。

```sh
octool-cli intel-iccmax-read --cpu 0
# 数值只是命令格式示例，不是电流建议；先确认当前 CPU / 供电轨配置
octool-cli intel-iccmax-set --cpu 0 --amps 100.25 --apply
```

低 11 位按 0.25 A 解码；bit 31 原样报告，模式语义未验证。新设置限定 1..511.75 A，必须恰好是
0.25 A 的倍数；不静默取整、不截断。此范围来自 FSP 的 4..2047 编码范围，不是安全工作范围。
OC lock、bit 31 设置或小于 1 A 的旧编码只读。FSP 将零定义为 HW default，本实现不推导其实际电流。
本实现不切换 unlimited，不用最大值自动解锁电流限制。

## 协议依据

[固定来源清单](validation/intel-iccmax-sources.json)记录完整文件的 Git blob 和 SHA256。

- [PowerMonkey CPU 表](https://github.com/psyq321/PowerMonkey/blob/5ca34dc2e68226368a8fc9728340850a19a94043/PowerMonkeyApp/CpuData.c)
  明确列出上表型号和 stepping，并绑定 11 位 ICCmax 与 Alder Lake VR 表。
- [VR 表](https://github.com/psyq321/PowerMonkey/blob/5ca34dc2e68226368a8fc9728340850a19a94043/PowerMonkeyApp/CpuDataVR.c)
  将 PCORE / RING / ECORE 都绑定到拓扑 bits 11:8 的地址和 bit 12 的 SVID 标志。
  [发现过程](https://github.com/psyq321/PowerMonkey/blob/5ca34dc2e68226368a8fc9728340850a19a94043/PowerMonkeyApp/Platform.c)
  说明标志为零才是 SVID。未复用其 GT 标志的掩码/位移不一致处。
- [VFTuning](https://github.com/psyq321/PowerMonkey/blob/5ca34dc2e68226368a8fc9728340850a19a94043/PowerMonkeyApp/VFTuning.c)
  用 VR 地址作为参数，命令 16h 读取、17h 设置；[邮箱构造](https://github.com/psyq321/PowerMonkey/blob/5ca34dc2e68226368a8fc9728340850a19a94043/PowerMonkeyApp/OcMailbox.c)
  与 [传输](https://github.com/psyq321/PowerMonkey/blob/5ca34dc2e68226368a8fc9728340850a19a94043/PowerMonkeyApp/CpuMailboxes.c)
  区分接口参数和 busy 提交位。代码独立实现，不复制这些文件的实现。
- Intel 官方 [Alder Lake-S FSP](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/AlderLakeFspBinPkg/IoT/AlderLakeS/Include/FspmUpd.h)
  与同 blob 的 Alder Lake-P 头明确规定 IA ICCmax 的 1/4 A 单位。FSP 结构偏移不是寄存器地址。

原版 [ICCmax / VR 拓扑指令](intel-iccmax-research.md)也提取相同 IA 地址/标志和 11 位字段，
但会忽略 SVID 标志、错误与读回，另有 10 位快捷路径会写 MSR 601。这里不沿用那些行为。
新增型号绑定来自以上固定实现，不是 Raptor Lake 或本项目真机测试的证明。

## 模式位的证据冲突

PowerMonkey 的 VFTuning 注释提到 ADL 使用 bit 31，但 CPU 表的
`hasUnlimitedIccMaxFlag` 实际为 0。FSP 明确有 IA unlimited 配置项，却没有给出对应邮箱位域。
因此没有把 bit 31 解释为已确认的 unlimited 状态。只报告该位并在置位时禁止编辑，
设置只改 11 位电流字段，完整保留其它位。unlimited 读取语义与切换仍是剩余缺口。

## 事务与两端行为

读操作核验身份、虚拟化、MSR 能力和完整 FLEX_RATIO；命令 04h 查拓扑后，只对 SVID IA 地址
提交 16h，随后重查完整拓扑与身份/锁上下文。变化或任何错误使整份读数无效。
查询也会向 MSR 150 提交查询命令，不是纯读 MSR；不会提交 17h 设置。

设置先重新执行上述读序，对比完整旧数据、拓扑、CPU 身份和锁寄存器；仅替换低 11 位，
其余位保持原值。相同值不发设置命令。17h 最多提交一次，再用 16h 独立回读，连同
拓扑和身份/锁上下文全部一致才报告 verified。错误后不重发、不自动回滚，不写 MSR 601。
邮箱各轮询最多 100 次，另有事务期限和取消检查；其它程序的竞争不受进程内互斥保护。

JSON `before/after` 提供 `encoded_limit_amps`、`mode_bit31`、
`ia_svid_address`、完整原值、可编辑状态和错误。`unlimited_mode` 与 `effective_limit_amps` 始终为 null，
`mode_semantics_verified` 为 false；不将配置编码等同于实际限流值。无效快照数值统一为 null。
GUI 支持读取、复制、显式确认后设置；切换 CPU、读取失败或未验证写入会清空旧结果。

验证使用合成固件与明确的固定原始字，覆盖所有 2048 编码、16 地址、逐位上下文变化、
所有传输故障位置、255 种固件错误、busy、截止时间/取消及 GUI 关闭/切换竞态。
自动化与 Linux 安装兼容结果单独记录；未进行硬件电流、稳定性、供电轨效果或真机验收。
