# AMD Family 17h / 19h PStates 定义恢复

2026-10-18：GUI 的 AMD PStates 页和 CLI `amd-pstates --cpu N` 共用核心读取、
解码及错误处理。支持声明硬件 P-state 能力的 AMD Family 17h、19h、1Ah。
这是配置定义查询，不是瞬时频率测量，也没有增加 P-state 写入。

## 字段和读取范围

在所选逻辑 CPU 上检查厂商、family、扩展 CPUID 上限及 `80000007h:EDX[7]`，
然后读取 `C0010061h` 的非 boost P-state 范围及 `C0010064h` 起的定义。
保留完整 limit 原值；bits 2:0 为最小 P-state 编号，bits 6:4 为最大编号。
两者倒置时返回 `inconsistent_limit` / `-ERANGE`，不继续读取定义。
P0 到最大编号之间逐项读取；单行失败保留其错误，失败输出不进入解码器。

| 字段 | Family 17h / 19h | Family 1Ah |
|---|---|---|
| 启用 | bit 63 | bit 63 |
| FID | bits 7:0 | bits 11:0 |
| DFS 编码 | bits 13:8 | 不使用 |
| 配置 MHz | `200 × FID / DFS` | `5 × FID` |
| VID 展示 | bits 21:14 的低字节片段 | bits 32,21:14 的 9 位值 |
| Idd 原始值 | bits 29:22 | bits 29:22 |
| IddDiv 原始值 | bits 31:30 | bits 31:30 |
| bit 32 | 单独保留原始位 | 单独展示，亦计入 VID |

FID 小于 `10h` 不生成频率。旧布局仅解码 DFS `08h..1Ah` 以及
`1Ch,1Eh,20h,22h,24h,26h,28h,2Ah,2Ch`。AMD OSRR 的说明段提及 `30h`，
但紧随的值表将 `2Dh..3Fh` 列为保留，故实现采用表内无歧义范围。
其余非零 DFS 显示 `unverified_divider`，不声称在所有型号上均为保留值。
DFS 为零显示 `divider_off`；未启用定义显示 `disabled`。
这些情况的 CLI 频率为 `null`，GUI 为 `—`，不会伪装成测得的 0 MHz。

某些 19h 型号在 bit 32 扩展 VID。低字节和该位分别保留，不把低字节宣称为完整 VID，
也不据此转换电压或电流。Family 1Ah 原有解码保持不变。

## 原版差异与证据

[AMD OSRR 56255 Rev 3.03](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/56255_OSRR.pdf)
印刷页 129–131 描述 17h Models 00h–2Fh 的 limit、定义及 DFS 值表。
[Linux v6.12 cpupower AMD helper](https://github.com/torvalds/linux/blob/v6.12/tools/power/cpupower/utils/helpers/amd.c)
提供 Zen 旧布局和 1Ah 新布局的分支；其
[CPUID 检查](https://github.com/torvalds/linux/blob/v6.12/tools/power/cpupower/utils/helpers/cpuid.c)
用于交叉核对能力位。19h 的频率布局依据 Linux 实现及原 ELF 分支，不能把 17h 手册
的全部电压、保留位和写入要求直接推广到全部 19h 型号。

从已固定 SHA256 的原 ELF 提取 `AMD_Configuration::Get_PStates_Vec` 完整函数字节，
只在 Unicorn 中执行旧频率算术分支；位提取辅助函数为合成实现。
256 个 FID × 64 个 DFS 共 16,384 个输入验证了两个原版偏差：

- DFS `09h` 在原指令中使用除数 1.25；AMD 表中为 1.125。
  FID 165 时原版给出 3300 MHz，按表应为约 3666.666667 MHz。
- 原版算术分支截断为整数 MHz，且本分支不拒绝 FID 小于 `10h`。
  新实现保留小数并拒绝保留 FID。

回放没有执行完整原函数、原 GUI、MSR 包装或任何硬件访问。
原始输入、代码、PDF 和 Linux 文件的固定哈希见
[来源清单](validation/amd-pstates-legacy-sources.json)；
[分支回放结果](validation/legacy-amd-pstate-frequency.json)可由
`analysis/tools/legacy-amd-pstate-frequency.py` 重新生成。

## GUI / CLI 一致性

GUI 新增 FID、DFS、原始 bit 32 列，按 family 更新 VID 表头，并显示 limit 原值、
非 boost 范围及逐行错误。切换 CPU 会清空旧快照；即使在异步读取期间切走又切回，
也会丢弃此次旧请求结果，须重新读取。

CLI 原命令和 JSON schema 版本保留，增加 family/model/stepping、layout、
`frequency_is_measurement: false`、`vid_bit_positions`、`limit_raw`、`minimum`，
以及每行 `frequency_status`、`frequency_id`、`divider_id`、`raw_bit_32`。
读取失败字段为 `null`；任一行读取失败使进程报告失败，同时保留其它成功行。
配置频率现在允许小数，消费端不应将该字段强制转换为整数。

回归覆盖两种旧 family 的 65,536 组启用/FID/DFS 组合、全部 256 个 family 门控、
三种支持 family 的全部 min/max 组合、能力缺失、逐寄存器失败和保留位隔离。
CLI 增加 29 份进程级 JSON 场景；Qt 覆盖旧布局、切换新布局、异步失效和部分失败。
云端结果另见本批验证记录，离线测试不能替代真机验收。

P-state 写入、按具体型号的 VID/Idd 物理量换算、CPPC 实时控制和剩余 AMD/主板功能
仍未由本批实现；当前功能边界见[还原状态](recovery-status.md)。
