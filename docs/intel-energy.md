# Intel 节能控制：Energy Efficient Turbo 与 Race to Halt

2026-10-13：GUI 的 Intel Controls → **Energy optimization** 和 CLI 的
`intel-energy-read/set` 共用独立核心 `intel_energy`。本批云端验证进行中，尚未真机验收。

## 已恢复范围

只开放 GenuineIntel Family 6 / Model B7（Raptor Lake-S）、具有 MSR 能力的裸机。
通过显式选择的逻辑 CPU 访问 `MSR_POWER_CTL`（1FCh）；不推断已修改所有核心或整个封装。
打开窗口、切换页面或改变 CPU 不读取硬件，必须点击读取。

| GUI / CLI 字段 | 控制位 | 对外语义 |
|---|---:|---|
| Energy Efficient Turbo / `efficient-turbo` | 19 | 1 启用、0 禁用；硬件位的极性相反 |
| Race to Halt / `race-to-halt` | 20 | 1 启用、0 禁用；硬件位的极性相反 |

Energy Efficient Turbo 允许处理器采用能效优化策略，其作用范围受传统 P-state / HWP 模式及 OS 请求影响。
Race to Halt 倾向于更早完成工作、进入空闲状态。界面显示配置，不推导实际节能效果、频率或性能。
未把 FSP 中独立的 EnergyEfficientPState 属性等同于 bit 19，也没有开放相邻的 PROCHOT、
平台覆盖、Ring EE、C-state 或热保护控制位。

每次设置只接受一个字段，重新读取身份、能力和完整 64 位寄存器；不同旧值返回错误。
写前再比较一次完整寄存器，随后最多写一次，只改变选中位。其余 63 位保持不变。
写后重新检查身份、能力和全部 64 位；固件忽略写入、其它位变化或访问失败都不能报告 verified。
已存在的设置在重新核对后不写入。无自动重试、回滚或全核广播。

SDM 对这两个字段没有定义本功能应检查的 OC 锁；因此不把 `FLEX_RATIO[20]` 或相邻
PROCHOT 锁当成它们的权限位。寄存器访问失败如实返回，保留所有其它位。
事务串行化仅约束本进程，无法排除系统固件、内核或其它调参程序的并发修改。

```sh
octool-cli intel-energy-read --cpu N
octool-cli intel-energy-set --cpu N --field efficient-turbo --value 1 --apply
octool-cli intel-energy-set --cpu N --field race-to-halt --value 0 --apply
```

CLI 输出完整十六进制原值、两项配置、是否尝试写入、完成写入次数、完整回读及 verified。
错误读取的字段为 null；合法的全零预期值仍输出 `0x0000000000000000`。
无效参数在打开后端前拒绝。GUI 使用同一核心，支持读取、复制、单项确认和结果失效处理；
改变 CPU、读取失败、关闭窗口或异步操作目标改变，不会继续显示可编辑的旧快照。

## 原版与平台依据

固定原 ELF SHA256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。

| 原函数 | 地址 / 字节数 | 核实内容 |
|---|---|---|
| `EE_TURBO_DIS(bool)` | 380C60h / 123 | 读取 1FCh，只替换 bit 19；true 表示禁用 |
| `RTH_DIS(bool)` | 380CE0h / 123 | 读取 1FCh，只替换 bit 20；true 表示禁用 |
| `Rd_POWER_CTL_Tuner()` | 384370h / 82 | 将读取的高、低 DWORD 重建为 64 位 |

[可复现工具](../analysis/tools/legacy-intel-energy.py)、[固定函数字节](../analysis/fixtures/legacy-intel-energy.json)、
[静态指令](validation/legacy-intel-energy-static.json)、[434 组执行结果](validation/legacy-intel-energy-emulation.json)
均已保留。实验执行原函数的完整指令，MSR wrapper 是明确的合成替身，没有启动原程序或访问硬件。
测试包含每个 64 位单独置位、混合字、零/全一、两个 bool 输入与 wrapper 成功/失败。
另外遍历邻近五个控制位的全部组合。

原 setter 忽略读写 wrapper 的结果且无回读。若失败的读取不填充输出参数，原 setter 继续以
预先清零的输出构造写值，可能同时清除其它控制位。这是原指令实验的观测，不会移植到新实现。
原 reader 对失败且未写输出的未初始化局部值没有建立确定语义。

- [Intel SDM Volume 4，335592-085US](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf)，
  §2.17、Table 2-39，印刷页 2-354、2-356/357：B7 适用关系、两项 R/W 位和禁用极性。
  已渲染并查看完整相关页，未混用后续 Core Ultra 表。
- [Intel Raptor Lake-S FSP FspsUpd.h](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspsUpd.h)：
  `EnergyEfficientTurbo` 为独立设置；`RaceToHalt` 明确引用 MSR 1FC bit 20。结构偏移不是 MSR 地址。
- [来源和哈希](validation/intel-energy-sources.json)。

增强 TVB 的 FSP `Etvb` 属性说明不包含可用的运行时寄存器控制位，本轮没有据此开放增强 TVB。
Vmax、逐物理核心 TVB、其它 Intel 型号和其余 AMD / 主板调参仍未全部恢复。

## 验证

共同核心新增 12 场景组，两字段分别覆盖设置/不变、每一位旧值冲突、身份变化、各 CPUID / MSR
故障位置、完整回读逐位损坏、写入被忽略、取消和超时。
GUI 增加 6 项 Qt 测试，包含模态确认中的 CPU 变化、读写中的目标变化与窗口关闭。
CLI 增加 16 组参数拒绝和 56 份功能/错误 JSON 报告。云端验证完成后记录同一提交的实际结果；
这些测试不代表硬件节能效果或稳定性测量。
