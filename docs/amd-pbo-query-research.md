# REA：VF 数据来源、SoC helper 与固件状态查询

2026-10-26，实际完成 [REA 任务 40257718519](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40257718519)，
固定 REA 6.7.0 / Ghidra 12.1.4，核对十个函数的身份、完整符号字节和工具记录。
原 ELF SHA256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
Ghidra 地址比原 ELF 偏移多 `100000h`；混合调用约定造成的伪代码参数缺失由原始指令复核。
七个函数的 REA 范围短于原 ELF 符号，差异逐项记录；实际回放使用完整符号字节，不把反编译范围视为完整性证明。
函数身份、字节哈希、公开来源 blob 与产物摘要见[机器记录](validation/amd-pbo-query-rea.json)。

| 原函数 | ELF 偏移 | 本轮作用 |
|---|---|---|
| get_amd_freq_vid | `6ECE40` | 确认直接 MSR 数据源及其错误边界 |
| tr5_loop_boost_curve / tr5_get_boost_curve / tr5_get_both | `6EEBF0` / `6FD190` / `6FEC20` | 复核 VF 调参调用路径；未获得独立的 PM 字段布局 |
| stream_vftf | `2ECA80` | 确认消费既有 PM 行并压缩输出，不能用结果序号认定物理核心 |
| ForceSocVid / UnForceSocVid | `221610` / `221A60` | 核对真实 PCI 参数及失败后输出行为 |
| get_co / set_core01_rails | `239BC0` / `239990` | 复核既有曲线查询和 rails 事务证据；没有补足双 rails 协议 |
| ln2_en | `4BF430` | 确认这是主板 EC bank 2 的位读取，与新增 SMU 查询分开 |

## VF 与逐核/CCD 表

`get_amd_freq_vid` 读取 MSR `C0010293`，只取低 32 位中的 FID `[7:0]`、DID `[13:8]`、VID `[21:14]`。
原指令的频率整数计算是 `((FID × 1000000 / DID) × 200) / 1000000`，每次除法均截断；
另一结果是 `245 + 5 × VID`。该 VID 只有八位，不能等同于 rails 打包的九位字段。
520 次合成实验包含全部 64 个 DID、256 个 VID 和读取失败后保留的栈内容：
原函数忽略 `RdmsrTx` 返回值，DID 为零时在 `6ECE96h` 发生整数除零。
这些是原程序行为，不是 Shimada MSR 格式或真实电压的独立验证。

`stream_vftf` 按 `Voltage`、`Temp`、`Freq Ghz`、`Eff Freq` 查找既有行索引，调用
`refresh_monitor(0)` 和 `get_pm(4)`，随后使用对象 `+928h` 的浮点向量。
三个输出乘以 1000 并向零截断，温度直接截断；频率为零的槽会被略过，四个输出向量同步压缩。
例如合成频率 `[4.0, 0.0, 4.5]` 产生 `[4000, 4500]`，没有附带原槽号 `[0, 2]`。
改变合成刷新边界的返回寄存器不会替换这些既有行；这不等同于证明真实刷新函数的错误合同。
在显式零初始化索引的合成对象中，频率标签缺失还会保留索引 0，误用 Voltage 行。
六个消费实验同时覆盖零核心、行过短与完整三槽。刷新/采集边界是明确的替身，没有伪称完整 PM 硬件采集。

因此 VF 绘图结果不能作为原逐核/CCD 偏移的第二份独立证据。
此前[PM 长度约束](amd-curve-all-research.md)中的越界、重叠和字段冲突仍未解决，
本批没有根据曲线输出顺序补造核心编号、CCD 编号或瓦特值。

## SoC 与 rails

原 ForceSocVid 把传入的原始字写到选中的 **response 寄存器**，然后向 command 发送 `FD`、`14`；
UnForceSocVid 将 response 写零，并发送 `FD`、`FD`、`15`。
两者最终都先读 argument、发布输出，随后才读 response；argument 中还被写入对应的 `14` 或 `15`。
32 个场景交叉四种合成 PCI 平台与四种 response，另有全部 34 个 PCI 故障位置实验。
busy、拒绝或全 1 的响应同样伴随已发布输出，故障注入也没有阻止函数继续执行。
不能把这些 TEST helper 当作已确认的电压设置、解除或 rails 恢复入口。

独立 [ZenStates-Core 固定提交](https://github.com/irusanov/ZenStates-Core/tree/869b7e82572337e5a673e92230edc92bddf0e44b)
中的 `SetOverclockCpuVid` 仅接收一个不透明整数，没有独立定义本项目原版双 rail 字段、物理目标、范围或回读。
既有 `set_core01_rails` 的 `61→5D` 和主动写 response 行为仍不构成可靠确认。
rails 保持[离线编码预览](amd-pbo-fmax-layout.md)，未开放硬件写入。

## 已接入的 GUI / CLI 查询

[固件查询用法](amd-pbo-queries.md)同步提供 LN2 状态与逐核性能原始字，限 Shimada `1Ah/08h`、PCI `1022:153a`、BDF 0。
固定公开实现分别确认 RSMU `A6` 和 `5C`；六参数初始化，检查 response，再读参数并复查平台身份。
LN2 只解释 `0/1`，未知字保留诊断但不显示为关闭；性能字不附加功耗、排名或物理核心含义。

原 `ln2_en` 实际调用 EC `Rd_ECRAM(4Ch, 2)` 并返回 AL 的 bit 3，已用全部 256 个字节复核。
REA 伪代码丢失了对象参数和 bank，所以没有照抄它的表面参数。
新增 SMU 查询不访问这个 EC 接口，也没有启用 LN2、改电压或隐含调参。

原指令回放合计 **848 次**：MSR 520、EC 256、SoC 32、故障 34、PM 消费 6；五项分析回归通过。
完整[工具](../analysis/tools/legacy-amd-vf-sources.py)、[固定字节](../analysis/fixtures/legacy-amd-vf-sources.json)、
[回归测试](../analysis/tests/test_amd_vf_sources.py)随源码提供；不启动原 ELF，不运行原固件或真实设备。

```sh
PYTHONPATH=build/reference-tools python3 analysis/tools/legacy-amd-vf-sources.py --output build/amd-vf-sources.json
PYTHONPATH=build/reference-tools python3 -m unittest discover -s analysis/tests -p test_amd_vf_sources.py -v
```

真实逐核/CCD 布局、双 rails 写入、动态 EC 控制和真机验收仍未完成。
