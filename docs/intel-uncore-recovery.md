# Intel Ring / LLC 倍率范围

GUI 的 Intel Controls → Ring / LLC range 和 CLI 的 `intel-uncore-read/set` 共用
`gui/core/intel_uncore`。当前明确开放 GenuineIntel family 6/model B7（Raptor Lake-S）
和 8F（Sapphire Rapids），要求 CPUID 宣告 MSR 能力且未宣告 hypervisor。
不凭主板名称推断型号，不把此范围扩展到 GNR/NVL 或其它型号。

逻辑 CPU 指定访问位置，设置作用于它所属的共享 Ring/LLC 域；不是逐物理核心设置。
显示的是配置倍率，不乘固定 BCLK 冒充实际 MHz，也不显示为实测频率。

| 寄存器 | 字段 | 操作 |
|---|---|---|
| MSR 620h | bits 6:0，最大倍率 | 读取；与最小倍率一并设置 |
| 同上 | bits 14:8，最小倍率 | 读取；与最大倍率一并设置 |
| 同上 | bit 7、bits 63:15 | 完整保留并验证读回 |

## 输入与提交

```text
octool-cli intel-uncore-read --cpu N
octool-cli intel-uncore-set --cpu N --minimum-ratio MIN --maximum-ratio MAX --apply
```

两项均须显式给出，范围为 `1 <= MIN <= MAX <= 127`。这是编码和输入范围，不是稳定性建议。
CLI 在打开设备前拒绝无效输入、缺失参数、重复参数和未带 `--apply` 的设置。
GUI 先读取当前范围，确认两个新值后提交。原始范围为零或倒置时仍可查看，但不开放设置；
不猜测零编码的默认/恢复含义。

提交前重新识别 CPU、检查能力及虚拟化标志，读取完整旧值，再在共同寄存器更新层比较一次。
身份或任意旧位变化即停止。只向 620h 写入一次，同时更新两个边界，避免分两次设置出现中间倒置。
随后再次核对身份并完整读回 64 位；固件忽略、钳位、其它位变化、取消、超时或传输失败均不报告验证成功。
值相同也重新检查，但不重复写入。不重试、不自动回滚。

`verified:true` 仅代表此次配置读回一致；`hardware_effect_measured:false`。
OS 电源管理、其它进程仍可能同时或随后修改同一寄存器，进程内事务锁不能阻止这种竞争。
程序不会代替用户停用内核 uncore 驱动，也不会修改 OC mailbox 或缓存电压。

## 原版行为与交叉依据

固定原 ELF 的 `Wr_Ratio_cache(int)`，地址 `0x377db0`，352 字节，先通过 150h mailbox
更改 cache OC 倍率，再读 620h，将最低两个完整字节都替换为截断到 8 位的输入。
因此它同时改变最小、最大倍率，并覆盖两个字段旁的 bit 7/15；输入 128/255 等会设置这些位。
它忽略 MSR wrapper 返回，busy 轮询无期限，也不读回设置结果。重构版把 620h 范围控制与
已有的 cache 最大 OC ratio 分开，按明确的 7 位定义保留其它位。

- [完整原指令与代码哈希](validation/legacy-intel-uncore-static.json)。
- [48 组原指令实验与 busy 循环](validation/legacy-intel-uncore-local.json)：执行原 writer 和
  `get_32bit` helper；MSR/等待为合成边界，平台标志置零；不包含原界面或真实硬件执行。
- [Linux v6.12 官方 uncore 驱动](https://github.com/torvalds/linux/blob/v6.12/drivers/platform/x86/intel/uncore-frequency/uncore-frequency.c)
  提供 620h、两个 7 位字段、非零范围检查、保留位更新、B7/8F 支持表和 hypervisor 排除。
  [同版型号定义](https://github.com/torvalds/linux/blob/v6.12/arch/x86/include/asm/intel-family.h)
  确认两个 CPUID 型号。该驱动区分 die；本程序由显式逻辑 CPU 定位，不遍历其它 CPU 或封装。
- [Intel SDM Volume 4，335592-085US](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf)
  有 620h 的 LLC/Ring 最小和最大倍率描述。其它型号表中的条目仅作语义核对，
  本轮 B7/8F 访问支持的独立依据为上述官方 Linux 驱动。
- [来源及证据哈希](validation/intel-uncore-sources.json)。

共同核心回归覆盖所有非零有序编码对、保留位、两个型号、无效身份/能力、虚拟机排除、
旧值逐位变化、每个 CPUID/读写故障位置、完整读回逐位损坏、无修改跳过、取消和截止时间。
GUI 验证读取、目标失效、输入、取消、写后显示及失败清空；CLI 验证打开设备前拒绝、
同一核心调用及 JSON 空值/结果语义。编译与发行版结果以对应提交的验证记录为准。
两种 profile 尚无目标真机调参、实际频率或稳定性验收。
