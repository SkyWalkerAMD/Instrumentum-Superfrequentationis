# Intel DDRIO 低功耗时序和配置

GUI 的 Intel Controls → Memory timings 与 CLI `intel-memory-read` /
`intel-memory-decode` 同步增加 11 项 DDRIO 配置。限定已有的 Raptor Lake-S
06_B7 / PCI 8086:A700 裸机读取配置；新快照 v4 为 19 个寄存器、68 个字段。
这些是程序化时序及允许发送功耗状态指示的配置，不是当前功耗状态或实测延迟。

依据 Intel 官方 [743846-001](https://cdrdv2-public.intel.com/743846/743846-001.pdf)
§3.2.83（156–157 页），已查看原表格。PDF SHA256 为
`714e415546d36e2d1bc8832bf94db817b48085224d54bebe252b16e1e48081f7`。
没有借用其它代处理器的 DDRIO 地址，也没有将该寄存器称作 IOL。

## 位域与单位

寄存器为 MCHBAR + E0B8h、32 位。MC1 加 10000h，channel 1 加 0800h；
DDR4 只允许 channel 0。下表字段在快照中统一带 `DDRIO_` 前缀。

| 字段 | 位域 | 单位 |
|---|---|---|
| self_refresh_enable | 31 | flag |
| power_down_enable | 30 | flag |
| idle_enable | 29 | flag |
| CKE_valid_enable | 28 | flag |
| CKE_valid_length | 27:24 | DCLK |
| self_refresh_length | 23:20 | MC_DCLK |
| self_refresh_latency | 19:14 | 8*tCK |
| power_down_length | 13:10 | MC_DCLK |
| power_down_latency | 9:5 | tCK |
| idle_length | 4:1 | encoded |
| raise_CKE_after_exit | 0 | flag |

保留计数原值和倍数单位，例如 `21 / 8*tCK` 表示存储了 21 个 8*tCK 单位，
不显示成 21 tCK 或推算成 ns。两项 MC_DCLK 保持控制器域单位。
idle_length 的单位未在该表明确，因此仅显示编码。
沿用原读取器的保守策略：任一 Gear 参数除法覆盖位开启时，tCK 和 8*tCK
字段显示 encoded，不猜测实际 DRAM 延迟；DCLK、MC_DCLK 和 flag 不改变。

读取器保留零配置，即使手册对某个延迟建议不得为零，也不擅自补成默认值。
整字全 1 仍按现有未初始化/无效采样策略拒绝；这不是固件配置合规检查。
没有添加任何配置写入、训练命令、功耗模式触发或自动周期采样。

## 快照与故障

新的读取生成 `octool-intel-memory-v4`。v1（12/30）、v2（14/43）和
v3（18/57）继续导入、复制、保存及 CLI 解码，保持版本和原寄存器集合；
旧文件不会追加伪造 DDRIO 字段，也不会自动访问硬件补读。GUI 明确提示未捕获的字段。
版本和寄存器集合必须完全匹配；导入重新从原始字计算字段，忽略伪造的值和单位。

现场读取仍比较两组完整寄存器以及三次主桥/MCHBAR 上下文，总计 53 次硬件读取
和 3 次 CPUID 调用。新增寄存器的任一读失败、两次内容变化或中途取消都会丢弃
整份采样。二次一致不保证硬件原子快照。HAL、ABI 和内核模块未改变。

核心回归覆盖 DDR4/5、单位/覆盖标志、每个位域的全部编码、故障位置和四版快照互通。
GUI 验证显示、复制、零值、旧版切换、滚动到末行；CLI 验证真实进程的离线文件互通。
测试使用合成寄存器，不能代表目标真机的训练稳定性或功耗效果。
