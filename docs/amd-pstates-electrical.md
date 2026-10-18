# AMD PStates 电压与电流描述值

GUI 的 AMD PStates 新增 Voltage / current descriptors 视图，CLI 原有
`amd-pstates --cpu N` 同步增加 `electrical` 结果。两者共用核心，不增加硬件读写次数。
这些数值来自 P-state 定义：VID 编码所表示的电压，以及供 ACPI _PSS 使用的预期最大单核电流。
它们不是实时传感器读数、整包电流、TDC/EDC 设置或当前运行 P-state。

## 型号绑定与依据

固定 coreboot 提交 `c8dc4a3bd1f93a02c47768f4ef122f67bb1a8fe3` 的 CPU 表、
Kconfig、MSR 结构和 P-state 换算调用共同建立以下范围。
[完整来源、Git 对象与 SHA256](validation/amd-pstates-electrical-sources.json)已保存。
型号以 CPUID family/model 为准，沿用这些 CPU 表的所有 stepping 匹配规则；不使用商品名推断。

| CPUID family / model | 固件路径 | 编码 |
|---|---|---|
| 17h / 11h、18h、20h | picasso CPU 表及 Kconfig | SVI2，8 位 VID |
| 19h / 50h | cezanne CPU 表及 Kconfig | SVI2，8 位 VID |
| 17h / A0h | mendocino CPU 表及 Kconfig | SVI3，9 位 VID |

[SVI2 实现](https://github.com/coreboot/coreboot/blob/c8dc4a3bd1f93a02c47768f4ef122f67bb1a8fe3/src/soc/amd/common/block/cpu/svi2.c)
由 bits21:14 得到 VID；00h–F7h 为 `1550 - 6.25 × VID` mV，F8h–FFh 是关闭编码，
结果为 0 mV 且状态为 `voltage_off`。这个 0 不代表测量到电压为零。
[SVI3 实现](https://github.com/coreboot/coreboot/blob/c8dc4a3bd1f93a02c47768f4ef122f67bb1a8fe3/src/soc/amd/common/block/cpu/svi3.c)
组合 bit32 与 bits21:14；VID 0 为关闭编码，其余为 `245 + 5 × VID` mV。
不会据合法编码范围生成可写电压范围。

[ACPI P-state 实现](https://github.com/coreboot/coreboot/blob/c8dc4a3bd1f93a02c47768f4ef122f67bb1a8fe3/src/soc/amd/common/block/acpi/cpu_power_state.c)
从 P-state MSR 取得 IddValue，并在计算 _PSS 功率时按 IddDiv 选择相应缩放。
本实现保留独立电流描述量：bits29:22 除以 1、10、100，对应 bits31:30 的 0、1、2；
编码 3 不产生电流。电流和 VID 电压独立报告，不从两者进一步推断 TDP 或实测功耗。
AMD OSRR 56255 第 130 页确认其预期单核/ACPI 描述用途；该页本身没有提供除数表。

这是一条公开固件实现证据链，不声称已取得 SVI NDA 规范全文或完成真机电气验证。
此次检查的 Phoenix MSR 头仍带待更新标记，Genoa 路径也未在本批贯通全部选择条件，
故不开放这些型号；1Ah、其它 17h/19h 继续只报告已有原始位与频率。

## 状态与兼容性

- CPUID 声明虚拟化时不输出物理量，仍可读取已有原始定义。
- 未启用定义、SVI2 的 bits62:32 或 SVI3 的 bits62:33 非零时，不生成 VID/电压/电流描述值。
- IddDiv 保留编码仅使电流不可用，不抹掉合法 VID 电压。频率编码问题与电气字段分别判断。
- 读取失败和超出 P-state 上限的行不会进入有效输出；CLI 的 `electrical` 为 `null`。
- 未确认型号使用 `unsupported_model`，不尝试通用家族公式。

CLI 保留 schema 1 和原始 `vid_bits` / `vid_bit_positions` 语义：17h/19h 的旧字段仍为低字节片段。
完整型号专用 VID 放在 `rows[].electrical.vid_bits`，旁边是 `voltage_mv`、`voltage_status`、
`expected_core_current_a`、`current_status`。不可用数值为 `null`。
顶层增加 `hypervisor`、`electrical_encoding`、`electrical_is_measurement: false` 和电流用途标签。

GUI 默认保留原始定义视图；视图切换复用同一已读快照。切换 CPU 会同时清空两个视图，
即使异步读取期间切走再切回，也会丢弃旧请求。复制快照包含当前视图表头及描述用途。

回归覆盖五个型号的边界、全部 512 个 VID 编码、全部 IddValue/IddDiv 组合、
所有 family/model 编码组合、全部保留高位、16 个 stepping、虚拟化和读取失败。
GUI 测试同步核对 SVI2/SVI3、视图切换不访问硬件、部分失败与旧结果清空；
CLI 新增 27 份 JSON 场景。所有数据均为合成输入，没有硬件写入。
