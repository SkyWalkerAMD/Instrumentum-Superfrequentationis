# Intel V/F 单点偏移设置

GUI 的 Intel Controls → V/F points 与 CLI 的 `intel-vf-set` 共用 `readIntelVfEdit` /
`applyIntelVfOffset`。范围仍是 GenuineIntel family 6/model B7 的 Raptor Lake-S client 配置，
core/domain 0 与 cache/domain 2，明确选择一个固件点 1..15。点的倍率只读。
逻辑 CPU 选择执行位置，不是物理核心曲线编号，也不据此扩大到 W790/W890 或其它型号。

## 来源与载荷区别

[原版指令及 135 组实验](intel-vf-recovery.md)已经固定原 ELF 和 core/cache setter 的实际路径。
原 core setter 会先用 0x14/0x15 清除全局控制 bit3；cache setter没有这一步。
新实现只查询 0x14，core 的 bit3 为 1 时拒绝修改，不自动改变 override 模式。

[PowerMonkey 的固定版本 VFTuning.c](https://github.com/psyq321/PowerMonkey/blob/5ca34dc2e68226368a8fc9728340850a19a94043/PowerMonkeyApp/VFTuning.c)
146–162 行与 305–339 行分别给出点查询及设置的编码：点从 1 开始，0x10 查询、0x11 设置，
signed 11-bit offset 放在 data[31:21]，单位 1/1024 V。**设置的 data[20:0] 为零**；
查询低字节是倍率，不能把整个查询值作为点设置载荷。
因此，旧研究提到的“丢弃 data[20:0]”本身不是点设置编码缺陷；原程序忽略错误、
无限等待及 core 的隐式模式切换仍不能直接复用。

PowerMonkey 同时编程全域配置的做法没有移入本实现，其旧型号表也不能证明 B7 真机已验证。
[Intel Raptor Lake-S FSP](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h)
1729–1769 和 1826–1854 行提供 core/ring 的 15 项配置数组和作用域信息；
它不是运行时邮箱地址文档，15 只是候选上界。下载内容及固定提交的校验值见
[来源清单](validation/intel-vf-write-sources.json)。新逻辑独立实现，没有引入外部程序的运行依赖。

## 准备与提交

普通 `intel-vf-read` 保留原查询行为。GUI 先选单点并点击 **Prepare selected point**，
CLI 用 `intel-vf-read --cpu N --domain core|cache --point P --for-edit` 查询可编辑状态。
准备依次读取身份、MSR 能力、MSR 0x194、控制查询 0x14、该域的全域配置和选定点。
任一查询失败都不发布可编辑快照；有效但受限的配置仍可查看。

允许提交的条件是 OC_LOCK 清除、全域处于 Adaptive、全域目标及 offset 都为零；
core 另要求 per-core override 关闭。全域低字节倍率可以非零，保持不变。
这是本版为避免多种电压配置互相影响而采用的保守策略，不表示固件必然拒绝其它组合。
cache 不借 core 的 override 位改变自己的作用域，但仍核对整个控制值未变。

```text
octool-cli intel-vf-set --cpu N --domain core|cache --point P --value MILLIVOLTS --apply
```

参数范围 -1000..999.0234375 mV，按 1/1024 V 就近编码；范围是表示能力，不是稳定参数建议。
GUI 确认窗显示当前值、请求值、实际编码值及明确目标。无效输入或取消不访问硬件。
CLI 在打开设备之前拒绝缺失确认、点越界、无效域、非有限数、越界数和重复参数。

提交会重新读取全部准备上下文，并比较完整身份签名、锁寄存器、控制值、全域值和点值。
过期快照不写。相同编码值在重新检查后报告 `unchanged:true`，不发设置命令。
否则只发送一次选定点的 0x11；绝不发送 0x15，也不把全域配置自动复位。
随后重新读取上下文和整个点值：预期点值保留旧 data[20:0]，替换 offset。
固件状态为零、全部上下文一致、完整点值符合预期且最终期限/取消检查通过，才报告成功。

## 结果与验证边界

CLI 输出 `edit_context`、`after`、`write_attempted`、`verified`、`unchanged`、`stage`、
`submitted_raw`、`expected_point_raw`、`settings_completed`、`settings_firmware_status`。
后两项只描述设置命令，不把后续查询的状态冒充为设置状态；未提交/未完成时相关字段为 `null`。
无效快照的原值与解码值为 `null`。GUI 目标变化、查询切换或失败会清空旧编辑状态。

错误后不重试、不回滚；已尝试写入但未验证时明确报告这一点。每次 busy 等待最多 100 次，
外层仍有事务期限和取消检查。锁只保护本进程，不能排除其它程序/驱动的并发访问。
读取按顺序完成，并非硬件原子快照；仅复核选定点及上述全局上下文，没有逐点测量其它曲线点。
`verified` 表示配置读回相符，`hardware_effect_measured` 始终为 false，不表示电压实测或稳定性验收。

新增九组核心场景覆盖两域全部候选选择器、2048 种 offset 编码、只读条件、过期上下文、
每个传输故障位置、固件拒绝、32 位点回读逐位差异、取消和超时。
GUI 覆盖准备、输入、取消、设置、同值、配置变化及读回失败；CLI 覆盖相同核心和 JSON 结果。
这些使用合成硬件，不访问本机 MSR。其它型号、逐物理核心模式、Xeon server 电压域仍待恢复。
