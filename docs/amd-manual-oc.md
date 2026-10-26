# Shimada 手动 OC 模式与频率

GUI 和 CLI 共用核心提供手动 OC 模式切换、全核频率、明确单核及同一 CCD 内选定多个固件核心的频率设置。
限定 AMD family `1Ah` / model `08h`，PCI `1022:153a`，domain 0 / BDF 0。
模式位可以回读比较；频率只能确认固件是否接受命令，当前频率和真机效果尚未验证。

| 操作 | RSMU 消息 | 参数 0 | 验证范围 |
|---|---|---|---|
| 开启 / 关闭手动 OC | `5Dh` / `5Eh` | 0 | 重新读取 `6Fh` 的 bit 16 |
| 全核频率 | `5Fh` | MHz | 固件响应 1，未验证实际频率 |
| 单个固件核心频率 | `60h` | `(CCD << 28) \| (core << 20) \| MHz` | 固件响应 1，未验证物理目标及实际频率 |
| 同一 CCD 内选定核心 | 每个核心一条 `60h` | 按显式列表逐核编码 | 每核单独确认；首个错误后停止，保留部分完成结果 |

CCD `0..15`、core `0..7`、MHz `1..1048575` 是编码边界，不代表目标存在或推荐运行值。
固件核心编号不等于 Linux 逻辑 CPU 编号。暂不推断禁用核心、CCX 或 APOB 映射。
同一 CCD 的操作需要显式列出目标核心；不根据逻辑 CPU 数量自动生成列表。
模式切换会影响 boost、电压、功率及稳定性；关闭模式不能保证所有设置恢复默认。

## 使用

```sh
octool-cli amd-pbo-capabilities --cpu 0
# RAW 应替换为最近查询所得的完整 capabilities_raw；不是固定常量。
octool-cli amd-oc-mode-set --cpu 0 --enabled 1 --expect-caps RAW --apply
octool-cli amd-pbo-capabilities --cpu 0
# 下列仅演示语法，5000 MHz 不表示适用于你的处理器。
octool-cli amd-oc-frequency-set --cpu 0 --scope core --ccd 0 --core 0 --mhz 5000 --expect-caps RAW --apply
# 同一 CCD 的显式核心列表；0,2,7 只是语法示例，不证明这些目标存在。
octool-cli amd-oc-frequency-set --cpu 0 --scope cores --ccd 0 --cores 0,2,7 --mhz 5000 --expect-caps RAW --apply
octool-cli amd-oc-frequency-set --cpu 0 --scope all-cores --mhz 5000 --expect-caps RAW --apply
# 关闭模式是单独操作，也需新查询的完整能力字。
octool-cli amd-oc-mode-set --cpu 0 --enabled 0 --expect-caps RAW --apply
```

每次设置前重新查询；频率命令要求状态 bit 16 已置位，不自动开启 OC。
bit 0 仅表示手动 OC 可用；启用前必须具备该能力，不能用它判断模式已开启。
CLI 同时输出 `manual_oc_available` 和 `manual_oc_enabled`，GUI 分别显示两项。
GUI：AMD tuning → Read OC capability → 选择请求模式或频率范围 → Apply。
单核默认输入留空；切换全核范围会清空并禁用单核选择。提交后须重新读取能力。
选择 `Selected cores in CCD` 后，在同一核心输入框填入严格递增的十进制列表，如 `0,2,7`。
不接受空列表、重复、倒序、空格、范围简写或 `all`。切换范围清空核心输入，防止沿用旧选择。
确认期间改变目标或输入时终止操作，异步期间改变目标时丢弃旧目标结果。

共用核心先检查 CPU/PCI 身份，重新查询并比较整个能力字，再次确认身份后发送一条设置命令。
使用 B8/BC RSMU `03B10924/03B10970/03B10A40`，六参数全部初始化。
多核操作按升序处理，**每个核心**都重新比较完整能力字并确认身份。任何一步失败立即停止，
不会向剩余核心发送命令。前面核心的改变可能保留；这不是原子操作。
模式已符合请求时不发送设置；切换后只比较 bit 16，保留完整前后能力字，允许其它能力位变化。
发生超时、取消或 I/O 错误即终止，不重试或回滚，不发送隐含 VID、Scalar 重置、MP1 回退或 `50200=1`。

模式结果中的 `verified=true` 仅表示模式位匹配。频率始终输出 `verified=false`、
`current_frequency_mhz=null`、`readback_supported=false` 和 `physical_mapping_verified=false`。
两者的 `hardware_effect_measured` 均为 false；`accepted=true` 不证明设置实际生效。

`--scope cores` 还输出 `visited_mask`、`attempted_mask`、`accepted_mask`、`failed_core` 和逐核 `cores` 明细。
`visited` 表示进入该核心处理，可能在能力读取时就失败；`write_attempted` 不保证硬件收到命令。
未处理核心的错误、阶段、参数、响应均为 null；频率回读始终为 null。
`completed` 仅表示整个命令序列正常结束。即使最后一条命令已收到成功响应，随后取消仍会返回错误并保留接受状态。
GUI 同样逐核展示接受、尝试未确认、未发送状态，出现部分完成后必须重新读取能力再手动决定下一步。

## 协议依据与原版研究

2026-10-26 根据 [ASUS 1317 固件研究](amd-smu-firmware-research.md)修正模式位：
SMU `0x00732e00` 的 `6Fh` 将运行状态编码在 bit 16，`5Dh/5Eh` 改写同一状态字节。
此前根据公开枚举名称把 bit 0 当成启用状态是错误的；旧版模式验证结果不能视为有效。
此结论来自静态指令和数据交叉引用，尚无目标真机验证。

公开实现固定于 ZenStates-Core `869b7e82572337e5a673e92230edc92bddf0e44b`：

- [Shimada 设置表](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)。
- [模式切换](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetOcMode.cs)。本项目不复制其关闭 OC 后额外重置 Scalar 的副作用。
- [全核频率](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetFrequencyAllCore.cs)、[单核频率](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetFrequencySingleCore.cs)、[MakeCoreMask 与能力定义](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Cpu.cs)。输入越界时拒绝，不沿用掩码截断。

原 ELF `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10` 的
enableocmode、amd_disableocmode、Wr_AMD_freq、per_ccx_freq、one_core_freq2、两个 fast 入口、Wr_ocvid、rails 与 Qt apply 槽
已完成独立 [REA 研究任务](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39553845129)。
十二函数的 1217 条原指令回放及单核编号换算冲突见[研究记录](amd-manual-oc-research.md)。
原版 BIOS/MP1 混合路径、UI 隐式电压设置和核心循环不直接用作新平台协议。
REA 伪代码需要原指令交叉核对，不能替代硬件验收。

## 验证范围

核心测试覆盖全部 20 位全核频率编码、128 个固件核心选择及频率边界，平台/能力不匹配、
完整能力字变化、模式回读不一致、固件拒绝、每个访问位置的失败与取消、超时和命令允许列表。
CLI 覆盖 JSON 类型、成功/失败语义及打开硬件后端前的参数拒绝。
GUI 覆盖模式读取与设置、单核/全核频率、空输入、范围切换、确认期间修改、陈旧能力与异步目标变化。
多核补充覆盖所有 255 个合法选择列表、稀疏及八核选择、每个位置的拒绝/能力变化/身份变化，
中间核心各访问位置的故障与取消，以及两个前端的部分完成报告。
固件替身即使忽略写入并返回成功，模式验证仍会失败，频率仍只报告接受。
