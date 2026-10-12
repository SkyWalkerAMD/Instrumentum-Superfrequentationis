# Intel 刷新、自刷新与 ZQ 校准参数

GUI `Intel Controls → Memory timings` 与 CLI `intel-memory-read` /
`intel-memory-decode` 共用实现。本轮新增 14 个字段，当前快照 v3 共 18 个寄存器、
57 个字段。仍限定 Raptor Lake-S CPUID 06_B7、PCI 8086:A700、无 hypervisor，
只读已经启用的 MCHBAR。调用方式见[内存控制器说明](intel-memory.md)。

## 字段及依据

按 Intel 官方 [743846-001](https://cdrdv2-public.intel.com/743846/743846-001.pdf)
§3.2.89（162–163 页）、§3.2.91（164 页）、§3.2.93（165–166 页）及
§3.2.103（172–173 页）实现。已经逐页检查寄存器表格，PDF 校验值和页码见
[来源记录](validation/intel-memory-sources.json)。以下是 MC0/channel0 规范偏移；
MC1 加 10000h，channel 1 加 0800h。DDR4 仅支持 channel 0。

| 字段 | 寄存器 / 位域 | 单位 | 适用范围 |
|---|---|---|---|
| tREFIx9 | E438h / 31:24 | 1024_DCLK | DDR4 / DDR5 |
| Refresh_block_wait | E438h / 23:20 | MC_DCLK | DDR4 / DDR5 |
| Self_refresh_exit_debits | E438h / 19:18 | count | DDR4 / DDR5 |
| High_priority_refresh_on_MRS | E438h / 17 | flag | DDR4 / DDR5 |
| Count_tREFI_when_refresh_disabled | E438h / 16 | flag | DDR4 / DDR5 |
| Refresh_panic_watermark | E438h / 15:12 | tREFI_count | DDR4 / DDR5 |
| Refresh_high_priority_watermark | E438h / 11:8 | tREFI_count | DDR4 / DDR5 |
| Opportunistic_refresh_idle | E438h / 7:0 | DCLK | DDR4 / DDR5 |
| tXSDLL | E440h / 12:0 | encoded | DDR4 / DDR5 |
| tZQCAL | E448h / 44:32 | 2*tCK | DDR5 |
| tZQCS | E448h / 20:10 | tCK | DDR4 |
| tZQLAT | E448h / 20:10 | tCK | DDR5 |
| tCKESR | E4C0h / 57:52 | encoded | DDR4 |
| Self_refresh_tCSL | E4C0h / 57:52 | encoded | DDR5 |

`tREFIx9` 是寄存器字段名，其编码单位是 **1024 DCLK**；不能把名称解释成
9 × tREFI。`Refresh_block_wait` 明确定义为 MC DCLK，硬件不会根据 Gear 改变它。
两个 watermark 是 tREFI 计数阈值，两个 flag 是 0/1 开关编码，并非时钟周期数。

E448h 的同一低位域在 DDR4 表示 tZQCS、DDR5 表示 tZQLAT；GUI 对另一类型显示
`Not used`，JSON 对其输出错误码和空值。tZQCAL 仅对 DDR5 解释，保留原始
2*tCK 计数，不乘以 2 改写数值。Gear 除法覆盖开启时，tCK 和 2*tCK 都退回
`encoded`，DCLK、MC_DCLK、计数和标志不跟着换算。

手册未明确 E440h TXSDLL 与 E4C0h TSR 的单位，因此保留 `encoded`。
E4C0h 的 DDR5 tCSL 与 E050h 的 MC_DCLK tCSL 分开命名；不借用后者单位。
E4C0h 低 13 位 TXSR 的说明针对 LPDDR，不解释为 DDR4/DDR5 的退出延迟。

## 原始值与兼容

新增字段保留所有可表示的配置编码，包括零；不根据推荐值补默认值，也不将零
推断为读取失败。比如 panic watermark 的文档最大建议值为 9，但读取器仍保留
10–15 的原编码，不据此断言配置正确。旧有 DIMM 容量、rank 占用和主时序的
未初始化检查继续执行；整字全 1、位宽错误、重复或缺少寄存器仍拒绝。
这些读数描述程序化配置，不证明训练完成、内存稳定或测得实际延迟。

新读取输出 `octool-intel-memory-v3`。v1（12 寄存器 / 30 字段）和
v2（14 / 43）仍可导入、复制、保存、离线解码，保持原版本及字段集合。
旧快照不自动补读新增地址。版本和寄存器集合必须完全对应。

两次读取都必须包含 18 个寄存器，且中间和末尾复核主桥 / MCHBAR，合计
51 次硬件读取及 3 次 CPUID。任何访问失败、取消、地址窗口变化或原始值变化
都不发布部分结果。两次一致不保证原子性。CLI 离线解码不创建硬件后端；
GUI 更换目标、载入无效文件或读取失败会清除旧结果。

## 验证边界

合成测试覆盖各字段编码边界、DDR4/DDR5 别名、零配置、保留位、64 位高位、
三版快照双向兼容、伪造解码值重算、全部 51 个读取失败位置及采样变化。
GUI 和 CLI 使用同一核心；本轮不添加时序写入、校准触发或训练命令。
目标真机未验收，其它 CPU / 主桥组合、LPDDR、IOL、实时内存频率及板级
PMIC/VRM/EC 控制仍不在本实现范围内。
