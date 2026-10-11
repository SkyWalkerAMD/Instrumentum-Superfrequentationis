# XMP 用户槽与 EXPO 增强时序存储值

GUI 的 Memory / Motherboard 和 CLI `spd-decode` / `inventory` 共用有界解码器。
本功能读取已有 SPD 文件或内核暴露的 EEPROM，不写入、启用或应用内存档案。

## XMP 3.0 用户槽

仅在基础 SPD 有效、XMP 3.0 头完整且 CRC 有效时检查两个用户槽。
用户槽 1 位于 896..959，用户槽 2 位于 960..1023。每槽 64 字节，末两字节是
前 62 字节的 CRC-16/XMODEM（初值 0）。通过校验且 tCK 非零的非空白数据，
按厂家档案相同布局解释 4 项电压、10 项基础时序、8 对 ps/nCK 次级要求、CAS 和命令率。

用户槽没有已确认的启用位。厂家启用掩码仅决定三个厂家档案，不决定用户槽。
GUI 显示 `Enablement unknown`，JSON 中用户槽 `enabled` 为 null；它不是 BIOS 当前选择状态。
厂家名称区也不作为用户槽名称。没有根据非零 CRC、非零电压或时序推断档案已启用。

| storage_state | 含义 |
|---|---|
| not_captured | 未捕获该槽字节；不能确定缺席 |
| incomplete | 只捕获部分字节；保留原值，不解释数值 |
| all_zero / all_ff | 完整槽全为 00 / FF；仅报告字节模式，不推断固件行为 |
| overlap_expo | EXPO 标志覆盖用户槽 1；不把区域当成 XMP，也不计算 XMP CRC |
| stored | 完整非空白字节；进一步查看独立 error / crc，不能仅靠此状态确认有效 |

EXPO 标志存在时，即使 EXPO CRC 错误或版本未知，也保留区域占用关系。
用户槽 2 不与 EXPO 1.0 区域重叠，可独立解释。完整非空白用户槽 CRC 或 tCK 出错时，
CLI 退出码为 3，其它有效节仍保留；可选槽未捕获或只捕获一部分不会单独触发失败退出。

CLI 的原 `xmp.profiles` 数组仍只包含厂家档案；新增 `xmp.user_profiles` 数组，
避免破坏已有的厂家档案编号。用户槽含原始字节、捕获字节数、状态、校验和数值。
非零有效数据的 CRC 可以恰为零；回归使用此前研究中固定的反例检查这一点。

## EXPO 1.0 增强时序

两个 40 字节档案各自的相对偏移 24..39 存储八个小端时间字段，依次为
tRRD_L、tCCD_L、tCCD_L_WR、tCCD_L_WR2、tFAW、tCCD_L_WTR、tCCD_S_WTR、tRTP，单位 ps。
这里没有 XMP 3.0 式独立 nCK 下限，也不根据 tCK 推算或替代存储值。

仅在整个 EXPO CRC 有效、版本 1.0、对应基础档案已启用且 tCK 非零时解释这些存储字段。
增强功能启用位尚未确认，因此与基础 `values` 分开放入 `stored_enhanced_timings`，
`enhanced_timings_enabled` 固定为 null。GUI 同样单独标注增强启用状态未知。
块级 `enhanced_timings_decoded` 表示至少一个档案的存储字段已解释，不表示增强功能已生效。
零值原样保留，不标记为 Auto；未知版本、错误 CRC、禁用基础档案不发布这些数值。

## 依据与验证范围

- [固定字段布局](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/ddr5spd_structs.h)
  给出用户槽和 EXPO 的布局；[XMP 读取实现](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/xmp3.cpp)
  和 [EXPO 字段读取](https://github.com/edlf/DDR5SPDEditor/blob/6c9d015098f3f25e636ff7402cf955d4cc7fa2cd/expo.cpp)
  用于核对字段事实。独立实现有界解码，不复制或链接这些 GPLv3 源码。
- [既有校验反例](validation/xmp-user-presence-research.json)说明非零 CRC 不是用户启用或存在判断。
- [Kingston 自认证表](https://media.kingston.com/pdfs/memory/self-certifications/KF552C36BBEK2-32-EXPO.pdf)
  区分基础和增强时序及其时间单位；该表不提供字节偏移或增强启用位。
- [此前固定来源与样本](validation/ddr5-expo-sources.json)保存源文件哈希。Qt 与 C# 参考实现中的
  DIMMs-per-channel / EnhancedTimings 占位返回值没有被当成有效协议。

新增测试覆盖两个用户槽的独立数值、所有捕获长度、每个 CRC 区域逐位损坏、全零/全 FF、
非零有效数据的零 CRC、全部厂家掩码和版本、EXPO 冲突隔离、增强字段逐字节边界和启用门控。
GUI 检查新文件和错误后旧值清除；CLI 检查 JSON 与全程不创建硬件后端。
这些是合成数据和软件验证，未执行真机训练、档案写入或调参验收。

仍缺 XMP 用户槽启用契约、XMP 3.0 推荐 DIMM 数、EXPO 增强启用契约 / 1.1 / 用户档案、
档案写入和实际应用。其它 CPU、主板写入缺口继续见[功能状态](recovery-status.md)。
