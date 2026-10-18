# Intel ODT 矩阵与写数据延迟

GUI 的 Intel Controls → Memory timings 和 CLI `intel-memory-read` /
`intel-memory-decode` 共用 v5 解析器，增加 8 个 ODT 掩码和 3 个写数据延迟配置。
限定已有 Raptor Lake-S CPU 06_B7、主桥 8086:A700 裸机读取；目前共 21 个寄存器、79 个字段。

依据 Intel 官方 [743846-001](https://cdrdv2-public.intel.com/743846/743846-001.pdf)
§3.2.81（153–154 页）、§3.2.100（170 页），已逐页查看原表。
同时核对 §3.2.82（155–156 页）：ODT 说明提到的 `enable_odt_matrix`
未出现在公开的调度寄存器位域表中。因此不会猜测该位的位置，也不声称矩阵当前启用。
PDF SHA256：`714e415546d36e2d1bc8832bf94db817b48085224d54bebe252b16e1e48081f7`。

## ODT 掩码

MCHBAR + E080h 为 32 位，复位值 0。每个字段为四位掩码，表示访问指定目标 Rank
时选用哪些 ODT 引脚；掩码 bit 0–3 对应 ODT 引脚 0–3。GUI 显示十六进制，JSON
保存整数编码和 `odt_pin_mask` 单位。它们不表示电阻值，也不映射主板插槽标签。

| 字段 | 位域 |
|---|---|
| ODT_read_rank0_mask | 3:0 |
| ODT_read_rank1_mask | 7:4 |
| ODT_read_rank2_mask | 11:8 |
| ODT_read_rank3_mask | 15:12 |
| ODT_write_rank0_mask | 19:16 |
| ODT_write_rank1_mask | 23:20 |
| ODT_write_rank2_mask | 27:24 |
| ODT_write_rank3_mask | 31:28 |

目标 Rank 未占用时，GUI 显示 `Rank absent`；JSON 值和单位为 null，错误为 ENODEV。
原始寄存器字仍完整保留，不按占用信息修改掩码内的引脚位。
DDR5 的此矩阵用于非目标 ODT；读操作的目标 Rank 不应被终端匹配。
读取器展示已存编码，不检查固件电气配置是否合规，不由非零掩码推断实际终端状态。

## 写数据延迟

MCHBAR + E478h 为 32 位，复位值 3；高 31:13 为保留位。

| 字段 | 位域 | 单位 |
|---|---|---|
| Write_delay_add_tCWL | 11:6 | DCLK |
| Write_delay_subtract_tCWL | 5:0 | DCLK |
| Write_delay_add_1QCLK | 12 | flag |

前两项分别为添加、扣减的 DCLK 数量。最后一项是手册中 Gear2 下增加一个 1× 时钟
延迟的选项，仅显示其配置标志；在其他 Gear 状态下也不把该标志换算成已生效延迟。
保持 tCWL 自身的值与单位，不能直接将不同域的时钟数相加减生成“最终 tCWL”、IOL 或 ns。
读取时保留零以及全部六位编码，不将超出固件建议的值替换为默认值。
两个新增寄存器均沿用整字全 1 的无效采样拒绝策略；这不是完整的固件合法性检查。

## 读取、兼容和验证边界

两个寄存器均使用既有 MC1 +10000h、channel 1 +0800h 路由；DDR4 仅 channel 0。
两组完整读取加三次主桥/MCHBAR 上下文检查，共 57 次读取与 3 次 CPUID。
任一读取失败、两组不一致或中途取消，整份结果作废。二次一致不保证硬件原子性。
没有增加写寄存器、训练命令、自动采样或新的内核接口。

新读取保存 `octool-intel-memory-v5`；v1（12/30）、v2（14/43）、v3（18/57）、
v4（19/68）继续原样导入导出，不补读硬件或伪造缺少的字段。导入严格匹配版本与
寄存器集合，并从原始字重新计算字段，忽略外部填写的解码数值及单位。

回归使用独立合成字与逐位编码，覆盖 DDR4/5、全部非空 Rank 掩码、Gear 覆盖组合、
写延迟保留位、失败/变化/取消位置、GUI 旧结果清除及 GUI/CLI 五版快照互通。
这些验证不能替代目标真机的读数核对、内存训练或电气验收。
