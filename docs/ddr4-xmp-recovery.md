# DDR4 XMP 2.0 档案读取

Memory / Motherboard、`octool-cli inventory` 和 `octool-cli spd-decode --file module.spd`
共用有界解码器，显示两组 DDR4 XMP 2.0 档案。每组包括 VDD（mV）和 12 项存储时序
（均为 ps）：tCK、tAA、tRCD、tRP、tRAS、tRC、tRFC1、tRFC2、tRFC4、tFAW、tRRD_S、tRRD_L。
启用位表示 SPD 中该档案可用，不表示 BIOS 已套用。读操作不修改 SPD，不测量当前训练值。

## 格式与边界

| 区域 | 绝对偏移 | 解释 |
|---|---:|---|
| 标志 | 384..385 | `0c 4a`；不足 386 字节时存在性未知 |
| 配置 | 386 | bits 0/1 分别启用两组档案，其余位仅保留原值 |
| 版本 | 387 | 仅支持 `20h` |
| 独立时间基准 | 388 / 389 | 各档案 bits 3:2 为 MTB、bits 1:0 为 FTB；只接受两者均为 0，即 125 ps / 1 ps；高四位不解释 |
| 完整头 | 384..392 | 9 字节，完整捕获后才解释版本、配置和档案 |
| 档案 1 / 2 | 393 / 440 | 各 47 字节，分别需要捕获至 439 / 486 |

电压编码为 bit 7 × 1000 + bits 6:0 × 10 mV；只报告编码值，不推断 Auto 或可用电压范围。
时序为 MTB 计数 × 125 ps，加有符号二进制补码 FTB 修正。tRFC1/2/4 是小端 16 位
MTB 计数，不能套用 DDR5 的 ns 原始字段。tRAS/tRC 的高四位共享一个字节；
tRC 有 FTB 修正，tRAS 没有。零 tCK 或任一负时序会使该档案报错并清空数值；
其它零编码按原值保留，不推断缺省值。另一组完整且可解码的档案不受影响。

XMP 时间基准独立于基础 SPD 的 byte 17；已启用档案的未知时间基准返回 ENOTSUP。
禁用档案不解释残留时序或时间基准，也不会因残留无效值报错；完整捕获时保留原始字节。
基础 SPD CRC 失败时不解释扩展。档案截断、未知版本和不支持的时间基准分开处理。

## 完整性与前端

XMP 2.0 没有扩展 CRC，也没有可自定义档案名称。基础 SPD CRC 不覆盖 XMP 区域。
GUI 因此显示 `No XMP checksum provided`，不能将该标签理解为已验证或数据可靠。
打开新文件会清除旧档案；DDR4 页面不显示 DDR5 EXPO 区域。

CLI 沿用 `data.spd.xmp`，新增 `header_captured`、`configuration_raw`、头部 `raw_hex`
和可空 `crc_supported`。头部完整时 `revision` 可用，不再以是否检查 CRC 推断头部是否完整。
两组档案各含 index/offset/enabled/error、47 字节 raw_hex、timebase_raw 和带单位数值。
`name_supported=false`、`name_valid=null`、`name=null`；CRC checked=false，其余 CRC 值为 null。
不足标志长度时 `present=null`，标志不匹配时为 false；未捕获完整头时 revision/raw_hex 为 null。
DDR5 XMP 3.0 的原有名称、独立 CRC 和 EXPO 冲突处理保持可用。

任一已识别扩展出错，离线命令返回退出码 3，保留其它有效区域及各自错误。
`active_configuration_measured=false`；离线读取不创建硬件后端。

## 依据与验证范围

[Intel 公开的 MrcSpdData.h](https://github.com/tianocore/edk2-platforms/blob/9c9a4821e0866f219f1d03ba24a6a433ea408223/Silicon/Intel/CoffeelakeSiliconPkg/SystemAgent/MemoryInit/Include/Coffeelake/MrcSpdData.h)
用于核对头部、启用位、时间基准位、47 字节档案布局及字段类型。
[DDR4XMPEditor XMP 数据布局](https://github.com/integralfx/DDR4XMPEditor/blob/521be188e0aaf1a25ba13a7de25033dd9fbb2d42/DDR4SPD/XMP.cs)
及 [SPD 时间基准](https://github.com/integralfx/DDR4XMPEditor/blob/521be188e0aaf1a25ba13a7de25033dd9fbb2d42/DDR4SPD/SPD.cs)
交叉确认电压、偏移、125 ps MTB 和有符号修正。
[Intel XMP 功能对照](https://download.intel.com/newsroom/2021/manufacturing/12th-Gen-Blueprint-Series-Presentation.pdf)
确认 XMP 2.0 两组档案且无名称/CRC 功能。只提取格式事实，自行实现，不复制或链接上游实现。

已记录[固定来源哈希](validation/ddr4-xmp-sources.json)。Memtest86+ 的基础 XMP 偏移可供交叉检查，
但该固定版本将 byte 427 加在 tRAS 上；这里依据 Intel 字段定义将其用于 tRC，并设专门回归。
Intel 头文件的 tRRD fine 注释把 bank group 描述互换，字段名与布局仍明确，本实现只报告参数名。

合成夹具刻意为两组档案使用不同数值；测试覆盖全部 0..512 捕获长度、256 个配置字节、
256 个时间基准编码、每个有符号字段的全部 256 编码、电压编码和组合字段边界。
这些是格式和软件行为验证，不是物理内存稳定性验收，也不宣称已模拟原版 ELF 的 DDR4 XMP 函数。
CAS 位图解释、每通道 DIMM 数、套用/写入档案、当前训练值和板级控制仍不在本轮范围内。
