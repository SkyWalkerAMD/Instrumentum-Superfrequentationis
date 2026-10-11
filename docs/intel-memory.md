# Intel 内存控制器时序读取

GUI 的 Intel Controls → Memory timings 与 CLI 的 `intel-memory-read` 共用核心。
当前限定 Raptor Lake-S：CPUID family 6 / model B7、无 hypervisor 标志，并且
`0000:00:00.0` 为 `8086:A700`、PCI class `060000`。不把本页推广为全部 Intel 型号支持。

新增 30 个字段：CL、CWL、RCD、RP、RAS、WRPRE、RDPRE、PPD、FAW、RRD SG/DG、
REFSBRD、RFC、REFI，以及 RDRD/RDWR/WRRD/WRWR 各四种目标关系。
SG/DG 表示同/不同 bank group，DR/DD 表示同 DIMM 的不同 rank / 不同 DIMM。
DDR5 不使用 PPD，DDR4 不使用 REFSBRD，对应值为空并明确显示不适用。
WRPRE 是写命令到预充电的间隔，不能当作单独的 WR；不推算未确认的 WR/RTP/WTR。

## 使用

```sh
sudo octool-cli intel-memory-read --cpu 0 --controller 0 --channel 0 > memory.json
octool-cli intel-memory-decode --file memory.json
```

`--cpu` 是当前进程允许使用的逻辑 CPU，示例中的 0 需按机器情况选择。
controller 为 0 或 1；DDR4 每个 controller 仅 channel 0，DDR5 可选 0 或 1。
空通道的 rank 占用可能保持复位值 0Fh，因此额外要求 D80C/D810 的 DIMM L 或 S 容量非零。
这些是控制器寄存器索引，不直接映射为主板上的 A1/B2 槽位。
GUI 点击 Read once 才读取；可打开 CLI 报告、复制或保存快照。
GUI 保存的 JSON 可由 CLI 离线解码，离线操作不创建硬件后端。

12 个固定寄存器包括内存类型、DIMM 容量非空检查、rank 占用、调度配置及上述时序。
类型与通道不匹配、rank 空缺、未初始化值、未知布局、无访问权限或读取失败均不发布旧时序。
硬件读取比较两组完整寄存器，并检查其间的主桥身份与 MCHBAR；变化返回错误，不自动重试。
这能发现采样期间的变化，但不能保证硬件原子快照或证明训练稳定性。

## 单位和快照

通常显示手册定义的程序化 `tCK` 值。任一 Gear 参数除法覆盖标志开启时，只显示
`encoded` 原编码，保留 Gear2、Gear4、两项覆盖标志、command-stretch 与 N:1 编码。
不盲目乘 2/4，不从 command-stretch 单字段推断实际总线命令率，不输出猜测的 MHz 或 ns。

格式为 `octool-intel-memory-v1`，布局键为 `raptor-lake-s-743846-001`。
所有原始寄存器使用 16 位十六进制字符串，防止经过 JSON/Qt 时丢失高于 2^53 的精度。
offset 统一保存为 MC0/channel0 的规范偏移：共享的 D800 不加 channel 偏移，D80C 的 channel 1 是 D810（+4），时序区则为 +0800h。
导入时根据原始字重新计算字段，不信任外部提供的解码数值、内存类型和来源声明。
最多 64 KiB，拒绝缺项/重复寄存器、未知偏移、错误位宽、无效 UTF-8 和过深 JSON。
一次快照必须包含全部 12 个寄存器；不将不完整采样补零成成功结果。

## 依据和边界

依据 Intel 官方 [743846-001 桌面版手册](https://cdrdv2-public.intel.com/743846/743846-001.pdf)，
2022 年 10 月 Rev.001，标题明确为 Raptor Lake-S。已下载完整 PDF 并核对相关表格；
文件 SHA256 和对应章节见[来源记录](validation/intel-memory-sources.json)。
PCI ID 另核对 Linux v6.12 的
[`uncore_snb.c`](https://github.com/torvalds/linux/blob/v6.12/arch/x86/events/intel/uncore_snb.c)。
没有把移动版 764981 的页面当作本平台依据，也没有复制第三方程序的实现。

MCHBAR 只读 PCI 48h/4Ch，要求已经启用、基址非零且保留位符合定义。程序不会开启或重定位
该窗口。MC1 偏移 +10000h，时序区 channel 1 偏移 +0800h；不访问广播区域，不搜索未知地址。
所有硬件请求均为读取，沿用现有 Linux 后端，不修改 `.ko`、HAL 或 ABI。

本轮提供限定平台的时序读数与快照功能；没有恢复训练命令或时序写入。
其它 Intel 型号、RTL/IOL、频率/特殊 Gear 换算、板级 PMIC/VRM/EC/时钟和真正的训练控制仍有缺口。
自动化覆盖合成设备、故障位置、采样变化、离线输入和 GUI 生命周期；目标真机尚未验证。
