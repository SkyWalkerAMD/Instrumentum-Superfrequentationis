# DDR4 非对称内存容量

GUI Memory / Motherboard、CLI `spd-decode` / `inventory` 共用解析器，新增第二组封装、
密度比、两组 rank 数和容量分项。原始编码与 CRC 状态一并保留，不代表系统可用容量或训练结果。

依据 [JEDEC 21-C Annex L DDR4 SPD release 27A 原文镜像](https://studylib.net/doc/28564671/4-01-02-annexl-4r27a)
表 20、25、26、28、30–32 及 8.1.14.1。已核对网页提取的标准文字；官方入口未取得正文，
镜像整文件下载返回 403，未宣称保存完整标准或完整文件哈希。[来源与布局事实](validation/ddr4-asymmetric-sources.json)。

byte 12 bit 6 表示非对称；byte 6 / 10 分别描述两组封装。byte 10 bits 3:2 的密度等级
依表 26 换算，不能简单右移：16→12→8、32→24→16、12→8→4、24→16→12 Gbit。
未知密度、保留编码、位宽矛盾或未确认的 rank 组合不输出容量。

| 两组封装 | 总 package rank | 第一组 / 第二组 |
|---|---:|---|
| SDP / SDP | 2 或 4 | 1/1 或 2/2 |
| SDP / DDP | 3 | 1/2 |
| DDP / SDP | 3 | 2/1 |
| DDP / DDP | 4 | 2/2 |
| 2–8H 3DS / 2–8H 3DS | 2 | 1/1，各乘自身层数 |

各组以密度、数据总线/器件位宽和容量 rank 因子计算后相加；ECC 不计入。
三 Rank SDP+DDP 不能采用 DDR5 的奇偶平分方法。其它布局继续明确显示未解析。
对称模块忽略 byte 10；要求 SPD 基础主版本 1、至少 128 字节且基础 CRC 正确。

合成测试：16 Gbit SDP 一 Rank 加 12 Gbit DDP 两 Rank，x8/64-bit，
16384+24576=40960 MiB；交换封装后为 45056 MiB。8H+7H 3DS 的不同密度例为 217088 MiB。
六组核心测试覆盖密度表、所有封装字节、rank 边界、位宽/ECC、修订版和截断/CRC；
GUI 验证分项、切换与错误清空，CLI 验证十份新增报告。没有 SPD 写入。
