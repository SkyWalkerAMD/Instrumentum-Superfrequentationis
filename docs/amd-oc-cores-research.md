# CCD 内选定核心的频率设置：REA 研究

2026-10-21，使用实际 REA 6.7.0 / Ghidra 12.1.4 完成八个原函数的静态查询，
[任务 39559121955](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39559121955)。
参考 ELF SHA256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
原程序未直接运行，未访问寄存器设备；REA 记录及原函数字节哈希见[机器记录](validation/amd-oc-cores-rea.json)。

| 原 ELF 偏移 | 原函数 | 观察 |
|---|---|---|
| `21FFB0` | `amd_per_ccd_vermeer` | 固定八核循环；正常路径初始化六个参数，发送 BIOS `27h` |
| `21F140` | `per_ccx_freq_vermeer_fast` | 固定八核循环；只写参数零与命令，保留其它参数 |
| `2202B0` | `one_core_freq_vermeer` | 原平坦核心编号同时用除 8 和余 7，不能当成物理编号映射 |
| `21EF30` | `one_core_freq_vermeer_fast` | 同类编号冲突；部分输入会下溢到编码 CCD 15 |
| `220110` / `21F1F0` | 两个 `per_ccx_freq` 入口 | 较早平台走四核 CCX 路径，现代平台走八核路径；选择依赖原全局变量 |
| `220430` | `one_core_freq2` | 原单核平台分派；不能用其全局条件放宽新平台身份检查 |
| `752270` | Qt 按 CCD 应用槽 | 隐式开启 OC、设置电压、调用循环，然后显示 Applied |

REA 分析地址比上述偏移多 `100000h`；记录核对了精确入口、函数名及 ELF 原字节。
Ghidra body 长度可能不含函数内填充字节，因此另存 ELF symbol 大小和完整指令解码。
Qt/混合 ABI 伪代码的参数仍需按原指令核对，不把丢失的参数用作协议定义。

## 原指令补充实验

`analysis/tools/legacy-amd-oc-cores.py` 复用上一批固定哈希的完整原函数 fixture，
使用 Unicorn 执行有界原指令。两条路径 × 16 个 CCD 编码 × 八个命令写入位置 ×
三种替身返回值（0、FEh、FFFFFFFFh），共 **768 组**。
每组只改变一个 `Wr_SMU_arg` 命令写入边界的返回值，其余返回 1；两条路径仍发送全部八个核心。
两项本机分析回归通过，检查不同位置、完整参数初始化差异及后续核心仍被提交的现象。

这些是 I/O 函数边界的合成返回值，**不是固件响应仿真**，也不是一次真机总线故障。
结果说明原调用者没有据此终止循环，不能证明任何目标存在或写入生效。

公开 [ZenStates-Core Cpu.cs 固定提交](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Cpu.cs)
中的 `SetFrequencyMultipleCores` 会在单条命令失败时返回，但其 CCX 调用者仍固定传入八核。
本项目使用已有明确的 Shimada RSMU `60h` 编码，将目标列表交给用户显式提供。

## 对恢复实现的影响

GUI `Selected cores in CCD` 与 CLI `--scope cores --ccd N --cores 0,2,7` 使用同一核心函数。
列表严格递增、非空，不接受重复、空格或自动 `all` 展开。每个核心写入前检查完整能力字和身份，
按升序处理；首次错误后不再处理后续核心。结果保留每核是否进入处理、是否尝试发送及是否收到确认。
之前核心的改变可能保留，不做自动重试或回滚；也不隐式改变 OC 模式、电压或 Scalar。

适用范围仍是 Shimada AMD `1Ah/08h`、PCI `1022:153a`、domain 0 / BDF 0。
固件编号与 Linux CPU 编号不等价，尚无活动核心发现或物理映射验收。
全部频率结果保持 `verified=false`，实际频率为 null。
完整用法与结果语义见[手动 OC 文档](amd-manual-oc.md)。

逐核/CCD 功耗表偏移与长度冲突、rails 写入、原生 PM 传输、动态内存/EC 和其它平台仍未解决。
本批恢复的是同一 CCD 内明确核心集合的频率命令与部分完成报告，不把它计作遥测或电压恢复。
