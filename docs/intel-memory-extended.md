# Intel RTL 与掉电时序

2026-10-12：GUI `Intel Controls → Memory timings` 与 CLI `intel-memory-read` /
`intel-memory-decode` 同步扩展到 43 字段。平台限制仍为 Raptor Lake-S B7 / A700，
只读已经启用的 MCHBAR，不改变硬件设置。使用方式见[内存控制器说明](intel-memory.md)。

## 新增字段

依据 Intel 743846-001 §3.2.76（148–149 页）、§3.2.79（151–153 页）和
§3.2.82（155 页）。原始 PDF SHA256 与核对页记录于[来源记录](validation/intel-memory-sources.json)。
以下均为 MC0/channel0 规范偏移；MC1 加 10000h，channel 1 加 0800h。

| 字段 | 寄存器 / 位域 | 单位 | 适用条件 |
| --- | --- | --- | --- |
| RTL_Rank0 | E020h / 7:0 | QCLK | rank mask bit 0 |
| RTL_Rank1 | E020h / 15:8 | QCLK | rank mask bit 1 |
| RTL_Rank2 | E020h / 23:16 | QCLK | rank mask bit 2 |
| RTL_Rank3 | E020h / 31:24 | QCLK | rank mask bit 3 |
| tPRPDEN | E050h / 63:59 | tCK | DDR4 / DDR5 |
| tCSL | E050h / 53:48 | MC_DCLK | DDR4 / DDR5 |
| tCSH | E050h / 47:42 | MC_DCLK | DDR4 / DDR5 |
| tWRPDEN | E050h / 41:32 | tCK | DDR4 / DDR5 |
| tRDPDEN | E050h / 28:21 | tCK | DDR4 / DDR5 |
| tXPDLL | E050h / 20:14 | tCK | DDR4，慢速退出预充电掉电模式所用的参数 |
| tXP | E050h / 13:7 | tCK | DDR4 / DDR5 |
| tCKE | E050h / 6:0 | tCK | DDR4 / DDR5 |
| tCPDED | E088h / 60:56 | tCK | DDR4；零是合法编码 |

RTL 是读命令发出到第一块数据返回控制器的程序化延迟，按有效 rank 分别显示。
E020h 的高 32 位属于 LPDDR 增强子通道，保留在原始字中但不解释为额外的 DDR4/DDR5 rank。
空 rank 的 RTL 字节即使保留复位值也不构成读数，JSON 的 `encoded` / `unit` 为 null，
字段错误码为 `-ENODEV`。不适用当前 DRAM 类型的字段使用 `-ENOTSUP`。
这些字段不确认 DIMM 槽位名称、实际掉电模式、实际训练结果或超频稳定性。

QCLK、MC_DCLK 和 tCK 分别保留，不换算成频率、纳秒或 IOL。Gear 参数除法覆盖标志只使
tCK 类字段退回 `encoded`；RTL 和 CSL/CSH 已在控制器时钟域，不跟随该转换。
字段提取保留完整位宽，未把手册的推荐/支持范围当作用户可以安全写入的范围。

## 版本与一致性

新版 v2 在原有 12 个寄存器后增加两个 64 位寄存器 E020h、E050h，共 14 个。
实时读取仍先确认 CPU、主桥、MCHBAR 和通道存在性，再比较两组完整寄存器。
一次成功读取有 3 次 CPUID 与 43 次只读 PCI/MMIO 请求；任何请求失败或两组数据不同，
都不发布部分时序或上次结果，不自动降级为 v1。

旧版 v1 严格保持 12 寄存器 / 30 字段，导入、复制、保存及 CLI 往返均保留 v1。
在 GUI 中切换新旧快照会重建表格，旧文件不会残留上次的 RTL 或掉电字段。
所有导入值均从原始寄存器重新推导，外部声称的 rank mask、数值和单位不具有权威性。

回归覆盖 DDR4/DDR5、两个控制器和合法通道、15 种非空 rank 掩码、零值与保留位、
Gear 覆盖、v1/v2 互通和版本不匹配、全部读取故障位置、采样变化、GUI 清理和生命周期。
自动化使用合成设备；目标真机、其它型号、IOL、剩余时序、时序写入和训练控制仍未完成。
