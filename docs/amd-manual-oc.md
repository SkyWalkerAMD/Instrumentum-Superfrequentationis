# Shimada 手动 OC 模式与频率

GUI 和 CLI 共用核心提供手动 OC 模式切换、全核频率设置及明确指定固件 CCD/core 的单核频率设置。
限定 AMD family `1Ah` / model `08h`，PCI `1022:153a`，domain 0 / BDF 0。
模式位可以回读比较；频率只能确认固件是否接受命令，当前频率和真机效果尚未验证。

| 操作 | RSMU 消息 | 参数 0 | 验证范围 |
|---|---|---|---|
| 开启 / 关闭手动 OC | `5Dh` / `5Eh` | 0 | 重新读取 `6Fh` 的 bit 0 |
| 全核频率 | `5Fh` | MHz | 固件响应 1，未验证实际频率 |
| 单个固件核心频率 | `60h` | `(CCD << 28) \| (core << 20) \| MHz` | 固件响应 1，未验证物理目标及实际频率 |

CCD `0..15`、core `0..7`、MHz `1..1048575` 是编码边界，不代表目标存在或推荐运行值。
固件核心编号不等于 Linux 逻辑 CPU 编号。暂不推断禁用核心、CCX、APOB 映射，也不展开整个 CCD 的广播写入。
模式切换会影响 boost、电压、功率及稳定性；关闭模式不能保证所有设置恢复默认。

## 使用

```sh
octool-cli amd-pbo-capabilities --cpu 0
# RAW 应替换为最近查询所得的完整 capabilities_raw；不是固定常量。
octool-cli amd-oc-mode-set --cpu 0 --enabled 1 --expect-caps RAW --apply
octool-cli amd-pbo-capabilities --cpu 0
# 下列仅演示语法，5000 MHz 不表示适用于你的处理器。
octool-cli amd-oc-frequency-set --cpu 0 --scope core --ccd 0 --core 0 --mhz 5000 --expect-caps RAW --apply
octool-cli amd-oc-frequency-set --cpu 0 --scope all-cores --mhz 5000 --expect-caps RAW --apply
# 关闭模式是单独操作，也需新查询的完整能力字。
octool-cli amd-oc-mode-set --cpu 0 --enabled 0 --expect-caps RAW --apply
```

每次设置前重新查询；频率命令要求能力 bit 0 已置位，不自动开启 OC。
GUI：AMD tuning → Read OC capability → 选择请求模式或频率范围 → Apply。
单核默认输入留空；切换全核范围会清空并禁用单核选择。提交后须重新读取能力。
确认期间改变目标或输入时终止操作，异步期间改变目标时丢弃旧目标结果。

共用核心先检查 CPU/PCI 身份，重新查询并比较整个能力字，再次确认身份后发送一条设置命令。
使用 B8/BC RSMU `03B10924/03B10970/03B10A40`，六参数全部初始化。
模式已符合请求时不发送设置；切换后只比较 bit 0，保留完整前后能力字，允许其它能力位变化。
发生超时、取消或 I/O 错误即终止，不重试或回滚，不发送隐含 VID、Scalar 重置、MP1 回退或 `50200=1`。

模式结果中的 `verified=true` 仅表示模式位匹配。频率始终输出 `verified=false`、
`current_frequency_mhz=null`、`readback_supported=false` 和 `physical_mapping_verified=false`。
两者的 `hardware_effect_measured` 均为 false；`accepted=true` 不证明设置实际生效。

## 协议依据与原版研究

公开实现固定于 ZenStates-Core `869b7e82572337e5a673e92230edc92bddf0e44b`：

- [Shimada 设置表](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)。
- [模式切换](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetOcMode.cs)。本项目不复制其关闭 OC 后额外重置 Scalar 的副作用。
- [全核频率](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetFrequencyAllCore.cs)、[单核频率](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetFrequencySingleCore.cs)、[MakeCoreMask 与能力定义](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Cpu.cs)。输入越界时拒绝，不沿用掩码截断。

原 ELF `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10` 的
enableocmode、amd_disableocmode、Wr_AMD_freq、per_ccx_freq、one_core_freq2、两个 fast 入口、Wr_ocvid、rails 与 Qt apply 槽
已提交独立 [REA 研究任务](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39553845129)。
原版 BIOS/MP1 混合路径、UI 隐式电压设置和核心循环不直接用作新平台协议。
REA 伪代码需要原指令交叉核对，不能替代硬件验收。

## 验证范围

核心测试覆盖全部 20 位全核频率编码、128 个固件核心选择及频率边界，平台/能力不匹配、
完整能力字变化、模式回读不一致、固件拒绝、每个访问位置的失败与取消、超时和命令允许列表。
CLI 覆盖 JSON 类型、成功/失败语义及打开硬件后端前的参数拒绝。
GUI 覆盖模式读取与设置、单核/全核频率、空输入、范围切换、确认期间修改、陈旧能力与异步目标变化。
固件替身即使忽略写入并返回成功，模式验证仍会失败，频率仍只报告接受。
