# DDR5 EXPO 1.0 基础档案读取

GUI 的 Memory / Motherboard、CLI `spd-decode --file module.spd` 和 `inventory`
共用 `decodeSpd`，读取两组 EXPO 1.0 基础档案。每组包含 VDD/VDDQ/VPP（mV），
tCK/tAA/tRCD/tRP/tRAS/tRC/tWR（ps），tRFC1/tRFC2/tRFCsb（ns）。
这些是 SPD 存储要求，不表示 BIOS 当前启用状态、训练值、额定频率档位或稳定性结果。
零电压编码保留为零，不猜测 Auto 含义；没有 SPD 写入或套用档案操作。

## 布局与错误隔离

| 区域 | 绝对偏移 | 规则 |
|---|---:|---|
| EXPO 标志 | 832..835 | ASCII `EXPO`，不足 836 字节时存在性未知 |
| 版本 | 836 | 仅 `10h`，包括 `11h` 在内的其它版本暂不解码 |
| 配置 / 特性原始字节 | 837 / 838 | byte 837 bits 0/4 分别启用两组档案，其余位保留原值 |
| 档案 1 / 2 | 842 / 882 | 各 40 字节，小端字段；界面编号 1/2 对应认证表编号 0/1 |
| 共享 CRC | 958..959 | CRC-16/XMODEM，初值 0，覆盖 832..957 |

必须捕获完整 128 字节并通过共享 CRC 才解释档案；EXPO 截断、CRC 错误或未知版本时，
仍保留有效 XMP 和基础 JEDEC 信息。XMP 头损坏或不存在时，EXPO 可独立解码。
基础 SPD 校验失败则两种扩展都不解码。仅判断配置启用位，不用非零时序或非零 CRC
推断启用。禁用档案保留原始字节，不解释残留数值；已启用档案 tCK 为零时报错，
同一块中其它数值有效的档案仍可查看。

EXPO 占用 832..959，与 XMP 厂家档案 3 和用户档案 1 重叠。
即使 EXPO CRC 无效，也不把检测到 EXPO 标志的区域当作 XMP 3 解码。
若 XMP 头同时启用档案 3，则 XMP 报告冲突；校验有效的 EXPO 仍可查看。

## GUI 与 CLI

GUI 显示独立 EXPO 区域校验、原始配置字节、两组启用状态及带单位的数值。
打开新文件后不保留上一文件数值。截断数据与未检测到扩展分别显示。

CLI 在 `data.spd` 新增 `expo`，保留原有 `xmp.expo_present` 兼容字段：

- `inspected` / 可空的 `present`，以及可空的 `revision`。
- `error`、整块 `crc`（checked/valid/stored/computed）、128 字节 `raw_hex`。
- `configuration_raw` / `features_raw` 是未解释的原始字节，CRC 是否有效须同时查看。
- `profiles` 中每组有 index/offset/enabled/error、40 字节 raw_hex 和 name/value/unit 数值。
- `enhanced_timings_decoded`、`active_configuration_measured` 恒为 false。

任一已识别扩展出错，`spd-decode` 返回退出码 3，JSON 中仍包含其它有效节及各自错误。
顶层 SPD 错误按基础区、XMP、EXPO 的顺序选择首个错误，不覆盖分节结果。

## 格式依据与剩余范围

[DDR5SPDEditor 固定字段定义](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/ddr5spd_structs.h)、
[EXPO 读取方法](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/expo.cpp)
和该版本公开的 EXPO/混合 SPD 样本用于核对偏移、启用位、电压编码与基础时序单位。
仅使用格式事实，自行实现有界读取，不复制或链接其 GPLv3 代码。
其 hasData 使用非零计算 CRC，不能证明用户档案启用；本实现未采用此判据。

[Kingston EXPO 自认证表](https://media.kingston.com/pdfs/memory/self-certifications/KF552C36BBEK2-32-EXPO.pdf)
交叉确认基础时序 ps、刷新 ns 和电压单位，同时区分增强时序。该表不提供字节映射。
[固定来源和本地样本检查](validation/ddr5-expo-sources.json)记录内容哈希、原始标志与 CRC；
增强块公开样本与之前的混合 SPD 来自同一上游，不算独立真机验收。

增强时序的启用语义、每通道 DIMM 数、EXPO 用户档案、EXPO 1.1、XMP 用户档案、DDR4 XMP
及写入流程仍待还原。原版 ELF 上一轮已定位 XMP 读取函数；本轮 EXPO 是对内存功能的扩展，
不宣称已模拟或恢复原版 EXPO 函数。校验通过仅说明字节一致，不认证超频设置适用性。

新增回归覆盖两组数值及单位、全部捕获长度、EXPO 区域逐位损坏、全部配置字节、禁用残留、
未知版本、数值边界、XMP/EXPO 独立错误、GUI 清除旧数据和 CLI 不打开硬件设备。
