# DDR4 / DDR5 基础 SPD 时序补全

GUI Memory / Motherboard 与 CLI `spd-decode` / `inventory` 共用基础 SPD 解码。
本轮补充 DDR4 的三个写延迟字段、DDR5 的八对次级时序，以及 SPD 1.2 起新增的三对时序。
每项来自内存中存储的要求，不能表示当前训练值、稳定性或适合应用的配置。

## DDR4

只在 SPD 主版本 1、byte 17 低四位为零（125 ps MTB / 1 ps FTB）时解码。
下列字段实际不使用精细修正；不能误用其它时序的 FTB 字节。

| 字段 | 12 位计数的高四位 | 低八位 | 单位 |
|---|---|---|---|
| tWR minimum | byte 41 bits 3..0 | byte 42 | 计数 × 125 ps |
| tWTR_S minimum | byte 43 bits 3..0 | byte 44 | 计数 × 125 ps |
| tWTR_L minimum | byte 43 bits 7..4 | byte 45 | 计数 × 125 ps |

byte 41 的高四位不参与 tWR。新字段保留 0 和最大编码 511875 ps，
不把 0 解释为 Auto；这不改变其它已实现基础时序的零值处理。
未知主版本或不支持的时间基准不输出这三个字段，XMP 使用自己的时间基准。

## DDR5

每一对由两个小端字节（ps）和一个独立的周期下限字节（nCK）组成。
两个量分别保留，包括 0、65535 ps、255 nCK 和互不相称的编码。
不根据 tCK 换算周期，也不取两者较大值作为当前训练配置。

| 时序 | ps 偏移 | nCK 偏移 | 首次定义 |
|---|---|---|---|
| tRRD_L | 70..71 | 72 | 1.0 |
| tCCD_L | 73..74 | 75 | 1.0 |
| tCCD_L_WR | 76..77 | 78 | 1.0 |
| tCCD_L_WR2 | 79..80 | 81 | 1.0 |
| tFAW | 82..83 | 84 | 1.0 |
| tCCD_L_WTR | 85..86 | 87 | 1.0 |
| tCCD_S_WTR | 88..89 | 90 | 1.0 |
| tRTP | 91..92 | 93 | 1.0 |
| tCCD_M | 94..95 | 96 | 1.2 |
| tCCD_M_WR | 97..98 | 99 | 1.2 |
| tCCD_M_WTR | 100..101 | 102 | 1.2 |

只接受 SPD 主版本 1；最后三对另要求 byte 1 的低四位至少为 2。
版本 1.0 / 1.1 即使相应位置非零也不解码最后三对。
此版本来自基础区 byte 1，不使用模块信息修订版 byte 192 或 XMP 版本。
基础与 XMP 时序的偏移和顺序不同，不能复用同一张字段表。

## 两种入口与校验

CLI 保留 JSON 版本 1，通过 `data.spd.fields` 的 `name/value` 增加字段。
基础字段的值仍为十进制字符串，单位在名称中，例如
`SPD tRRD_L minimum (ps)`、`SPD tRRD_L cycle lower limit (nCK)`。
GUI 按相同名称显示，不改变既有 XMP / EXPO 的 `values`、CAS 或命令率结构。
重新载入旧版本、短文件或损坏文件时清除不再适用的行。

完整基础区 CRC 失败时不输出新时序，也不解码扩展。
DDR5 捕获 236..511 字节时可以解码已捕获字段，但明确标注基础 CRC 未验证。
不足现有最短长度（DDR4 128、DDR5 236）时仍返回捕获错误。
XMP/EXPO 错误不会隐藏有效基础区；本轮不新增硬件访问或写入。

## 固定依据与验证边界

[来源及样本检查](validation/spd-base-timings-sources.json)固定源文件提交、Git blob 和 SHA256。
[illumos DDR4 格式定义](https://github.com/illumos/illumos-gate/blob/c029af224ebf0e25e82df1c2347a9c20c97102cd/usr/src/lib/libjedec/common/spd_ddr4.h)
及其[字段解析](https://github.com/illumos/illumos-gate/blob/c029af224ebf0e25e82df1c2347a9c20c97102cd/usr/src/lib/libjedec/common/libjedec_spd_ddr4.c)
明确三个 12 位计数、共享高位和无 FTB。
[DDR5 字段定义](https://github.com/illumos/illumos-gate/blob/c029af224ebf0e25e82df1c2347a9c20c97102cd/usr/src/lib/libjedec/common/spd_ddr5.h)
及[解析入口](https://github.com/illumos/illumos-gate/blob/c029af224ebf0e25e82df1c2347a9c20c97102cd/usr/src/lib/libjedec/common/libjedec_spd_ddr5.c)
区分基础八对与 1.2 新增三对。仅使用格式事实，自行实现；未复制或链接上游实现。

两份公开 AQD 测试数据的原文及解码字节哈希、基础 CRC 已在本机核对。
其作者明确人工填充了部分身份字段，因此不把它们称作目标硬件实测。
核心测试只采用其中的 5 / 24 个时序字节放入合成模块，另使用独立标记检查字段串位。
新增测试覆盖各字段字节全部 256 编码、所有基础修订版、DDR4 时间基准、零值、边界、
截断与 CRC/扩展错误隔离；GUI、CLI 检查相同含义及旧行清除。

3DS 不同逻辑 rank 的刷新要求、EXPO 增强时序启用、XMP 用户档案、当前训练值及写入仍未补全。
