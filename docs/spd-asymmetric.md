# DDR5 混合 rank 容量

GUI Memory / Motherboard 与 CLI `spd-decode` / `inventory` 通过共同 SPD 解码器
支持 DDR5 非对称模块的两组几何组织和容量分项。它描述 SPD 中声明的模块，
不是安装可用容量、当前训练值或两条不同容量内存的组合。

| 字段 | 第一组（偶数 package rank） | 第二组（奇数 package rank） |
|---|---|---|
| 每 die 密度 / 封装 | byte 4 | byte 8 |
| 器件位宽 | byte 6 bits 7:5 | byte 10 bits 7:5 |

共同组织区 byte 234 bit 6 宣告非对称；package rank 总数来自该字节 bits 5:3，
子通道数与数据总线宽度来自 byte 235。要求基础区与共同组织区主版本均为 1，
至少两个 package rank；总数为奇数时，偶数编号组多一个 rank。
对每组独立计算并求和：

`MiB = Gbit/die × 128 × (data bus width / that group's device width) × package ranks × subchannels × logical layers`

单片和普通双芯片封装的层数因子为 1；3DS 使用该组的 2/4/8/16 层。
ECC 不计入数据容量。两组分别输出芯片密度/位宽/封装、rank 数、容量分项和总容量。
任何必要编码未知、保留、位宽大于总线或单 rank 却标为非对称时，不发布容量分项或总量；
仍保留第二组原始密度/封装/位宽代码和可解释的其它字段。
对称模块忽略未使用的第二组字节。DDR4 的非对称规则另行研究，没有套用此公式。

基础 CRC 错误清除解释结果。236..511 字节短捕获仍能读到所需字段，但明确标记
CRC 未验证；短于 236 字节不解析。更换文件后不会保留前一文件的容量或第二组字段。
仅第二组使用 3DS 时，也显示基础区的不同逻辑 rank 刷新下限；不将其当成训练值。

研究依据是 illumos 固定提交 `c029af224ebf0e25e82df1c2347a9c20c97102cd` 的
[DDR5 字段定义](https://github.com/illumos/illumos-gate/blob/c029af224ebf0e25e82df1c2347a9c20c97102cd/usr/src/lib/libjedec/common/spd_ddr5.h)、
[解析规则](https://github.com/illumos/illumos-gate/blob/c029af224ebf0e25e82df1c2347a9c20c97102cd/usr/src/lib/libjedec/common/libjedec_spd_ddr5.c)及
[容量说明](https://github.com/illumos/illumos-gate/blob/c029af224ebf0e25e82df1c2347a9c20c97102cd/usr/src/lib/fm/topo/modules/common/dimm/topo_dimm.c)。
[来源哈希](validation/spd-profile-geometry-sources.json)沿用已固定的完整文件。
该版本容量函数复用了第一组的器件宽度，而它自身的说明允许两组宽度不同；本实现按
各组独立字段计算，并用交换两组后总量不变、不同位宽的明确数字算例检验，未复制上游代码。

合成算例：两条子通道，第一组 24 Gbit/x8/4H 3DS、两个 package rank，第二组
16 Gbit/x16/单片、一个 package rank：196608 + 8192 = 204800 MiB。
这是解析测试，不代表实机 SKU。共同核心新增 6 组测试，覆盖密度/封装完整字节、
各位宽和总线组合、奇偶 rank、修订版、截断、CRC、非活动第二组和第二组 3DS；
GUI 与 CLI 检查同一分项、错误清空与短捕获标记。
