# SPD 档案元数据与堆叠内存组织

GUI Memory / Motherboard 与 CLI `spd-decode`、`inventory` 共用解码器。
本次补齐 DDR4 XMP 2.0 的限定 CAS 位图和推荐 DIMM 数、DDR4/DDR5 对称堆叠容量、
DDR5 3DS 不同逻辑 rank 的刷新要求，并修正 DDR5 四/八子通道的数量计算。
这些值描述 SPD 中的模块和档案，不是安装数量、当前训练值或稳定性测量。

## DDR4 XMP 2.0

| 字段 | 位置 | 已确认解释 |
|---|---|---|
| 档案 1 CAS | 397..400 | bits 0..17 对应 CL7..24，其余位保留 |
| 档案 2 CAS | 444..447 | 同上；byte 447 bit 7 不是基础 SPD 的范围选择位 |
| 档案 1 推荐 DIMM 数/通道 | byte 386 bits 3:2 | 代码 0..3 对应 1..4 |
| 档案 2 推荐 DIMM 数/通道 | byte 386 bits 5:4 | 代码 0..3 对应 1..4 |

采用 Intel 公开字段定义的范围，`cas.encoding="xmp2-cl7-24"`。
不能用编辑器扩大至 CL30 的做法证明保留位的含义；所有原始字节仍完整保留。
有保留位时列出已定义 CL，并标明 `reserved_bits_set=true`；空位图输出空列表。
仅解释已启用、完整且时间基准/时序有效的档案 CAS。XMP 2.0 没有扩展 CRC。

推荐数量是头部字段：已识别完整的 20h 头部且档案启用时，即使档案正文截断或时序
报错，也可输出 `recommended_dimms_per_channel`。禁用档案、未知版本和 XMP 3.0
为 `null`，不能从残留数据推断推荐值。GUI 明确说明实际安装数量未测量。
XMP 2.0 命令率仍不解释；基础 SPD、XMP 2.0 与 XMP 3.0 的 CAS 编码分别标识。

## 几何组织与容量

DDR4 密度代码 8/9 分别支持 12/24 Gbit，避免套用二次幂公式。
对称单片及有效多芯片封装按每 die 密度、数据总线宽度、器件宽度和 package rank 数计算。
3DS 单负载堆叠还需乘 die 数；多负载堆叠不再乘一次，以免重复计算 rank。

DDR5 byte 235 bits 7:5 是指数：代码 0/1/2/3 对应 1/2/4/8 子通道，4..7 保留。
数据宽度不包含 ECC。byte 4 的封装代码 0..5 分别是单片、双芯片和 2/4/8/16 层 3DS。
对称模块容量按下式计算：

`MiB = Gbit_per_die × 128 × (data_width / device_width) × package_ranks × subchannels × logical_layers`

3DS 的 `logical_layers` 为层数，单片/普通双芯片为 1。原始编码与解释字段分别输出。
基础组织要求 SPD 主版本 1；DDR5 的模块组织还要求 byte 192 的公共区主版本为 1。
未知版本、保留密度/宽度/封装/子通道编码、无效宽度组合或非对称 rank 不输出容量。
不把未知容量当作零，不给混合 rank 套用对称公式。

DDR5 3DS 封装代码 2..5 且基础主版本为 1 时，解码 bytes 48/50/52 开始的小端
16 位不同逻辑 rank tRFC1/tRFC2/tRFCsb 下限，单位 ns。它们属于基础区，不依赖
byte 192 的模块组织修订版；不误用为普通 tRFC 或 XMP 时序。零值按原始要求保留。

完整基础 CRC 失败时清除解释结果；DDR5 236..511 字节短捕获仍标注 CRC 未验证。
切换文件会清除旧容量、CAS、推荐数和刷新字段。

## 依据与验证

[固定来源及哈希](validation/spd-profile-geometry-sources.json)记录 Intel XMP 字段定义、
illumos DDR4/DDR5 格式定义、封装解析和 DIMM 容量规则。实现独立编写，不复制或链接上游代码。
测试使用独立期望值与合成 SPD，覆盖 CAS/推荐数/子通道完整字节编码、封装/密度、
版本、截断、校验失败及 GUI/CLI 输出；合成的 384 GiB 3DS 算例不代表实机配置。

尚未完成非对称/混合 rank 容量、XMP 3.0 推荐 DIMM 数/用户档案、EXPO 增强时序、
训练值、SPD/PMIC/VRM/EC/时钟写入。这一批实现不进行硬件写入。
