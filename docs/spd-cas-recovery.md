# SPD CAS 支持列表与 XMP 3.0 命令率

GUI Memory / Motherboard 和 CLI `spd-decode` / `inventory` 共用解码，新增 DDR4/DDR5
基础 SPD 的 CAS 支持列表，以及 DDR5 XMP 3.0 厂家档案的 CAS 列表和命令率。
列表来自位图，不通过 tAA/tCK 推算，也不表示当前训练的 CL、命令率或可稳定运行的配置。

## 已确认的格式

| 位置 | 位图字节 | 位到 CL 的映射 |
|---|---|---|
| DDR4 基础 SPD | 20..23 | bits 0..29；bit 31 为 0 时 CL7..36，为 1 时 CL23..52 |
| DDR5 基础 SPD | 24..28 | bits 0..39，对应 CL20..98，步长 2 |
| XMP 2.0 档案 | 档案内 4..7 | bits 0..17 对应 CL7..24，其余位保留，不使用基础 SPD 的范围位 |
| XMP 3.0 厂家档案 | 档案内 7..11 | 与 DDR5 基础区相同，对三个档案独立处理 |

字节内从最低位开始排列。DDR4 bit 30 是保留位，单独报告，不解释为 CL37/53。
保留位被设置时仍列出其它已定义位，并标明此情况；不声称整个编码有效。
零位图显示“None encoded”，与未读取、校验失败或未知修订版区分。

基础 CAS 仅解释 SPD 主版本 1。其它主版本保留位图原始字节，`decoded=false`，
不隐藏其它已经支持的基础信息。DDR5 236..511 字节短捕获可以列出 CAS，但基础 CRC
仍明确未验证。完整基础区 CRC 失败时不解码 CAS 或 XMP。

XMP 档案内 byte 60 的低四位是命令率代码：0 表示未指定；1/2/3 显示 1N/2N/3N。
4..15 显示未知代码，上四位只保留原值，不作其它语义解释。
未知代码不改写成零或 Auto，不隐藏其有效电压/时序，也不声称是可应用的配置。
只有完整、已启用、修订版 30h、头/档案 CRC 有效、tCK 非零且无 EXPO 重叠的档案
才输出这些字段。其它档案错误不影响有效档案。

## CLI 与 GUI

`data.spd.cas` 和 `data.spd.xmp.profiles[].cas`：

- 未解码区域、校验失败或未启用档案为 `null`。
- 捕获后包含 `offset`、`raw_hex`、`decoded`、`encoding`、`reserved_bits_set`、`latencies_nck`。
- 未知基础修订版保留原值，后两个解释字段为 `null`；合法零位图的列表是 `[]`。
- 列表为升序数字，单位 nCK。位图只表达支持集合，不指定当前使用其中哪一项。

`data.spd.xmp.profiles[].command_rate`：

- 未满足档案解码条件时为 `null`。
- 已读取时包含 `raw`、低四位 `code`、`recognized`、`cycles`。
- 未指定代码 0：`recognized=true, cycles=null`；1..3 给出对应周期。
- 未知代码：`recognized=false, cycles=null`，保留完整原字节与代码。

JSON 版本仍为 1；原有 `values` 数组内容和单位不变，XMP 3.0 仍有 30 项电压/时序值。
新增元数据中的未知含义不会单独改变命令退出码，调用方应检查相应状态。
GUI 每行最多显示六个 CL，避免长列表撑宽表格；复制快照保留全部行。
打开新文件或遇到错误后清除旧列表和命令率。

## 依据与限制

[固定来源与样本检查](validation/spd-cas-sources.json)记录提交、文件哈希和字节定义。
[illumos DDR4 解码](https://github.com/illumos/illumos-gate/blob/c029af224ebf0e25e82df1c2347a9c20c97102cd/usr/src/lib/libjedec/common/libjedec_spd_ddr4.c)
与 [DDR5 位图定义](https://github.com/illumos/illumos-gate/blob/c029af224ebf0e25e82df1c2347a9c20c97102cd/usr/src/lib/libjedec/common/spd_ddr5.h)
提供基础区的格式事实；[XMP 字段定义](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/ddr5spd_structs.h)、
[CAS 位映射](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/utilities.cpp)
及 [命令率访问器](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/xmp3.cpp)
提供扩展格式事实。未复制或链接这些上游实现。

DDR4 XMP 现按 Intel 公开字段定义限定解码 CL7..24，显式标识 `xmp2-cl7-24`；
其余位仍保留并报告，不能把基础区范围选择位套给它。头部推荐 DIMM 数也已支持，
见[档案元数据与组织](spd-profile-geometry.md)。XMP 3.0 推荐 DIMM 数、XMP 用户档案、
EXPO 增强时序、当前训练值、SPD/主板写入仍未还原。
此项功能不进行硬件写入、内存训练或稳定性测试。
