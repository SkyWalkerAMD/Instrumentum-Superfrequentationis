# AMD 逐核与 CCD 选核曲线设置

2026-10-21：GUI 和 CLI 共用 `gui/core/amd_curve.cpp`，新增单核及同一 CCD 内明确选核的
CO count 设置。限定 AMD family `1Ah` / model `08h`、PCI `1022:153a`、
domain 0 / BDF `00:00.0`。本实现尚未在真实 Shimada 硬件上验收。

## 使用范围

固件 CCD 为 0–15，core 为 0–7；支持 `0,2,7` 这样的升序、不重复选核列表。
这些是编码边界，不证明具体核心存在，也不把 Linux CPU / CPUID 编号自动映射为固件目标。
“CCD 选核”表示分别设置用户选定的核心；不是新的 CCD 广播命令。

数值为整数 CO count `-50..50`，不是 mV，也不是对任意机器稳定性的保证。
新设置仅发送 RSMU `06`，查询和回读为 `A3`；参数为
`CCD[31:28] | core[23:20] | uint16(count)`，CCX 与保留位清零。
沿用原查询恢复的 B8/BC 间接访问，response/command/argument 为
`03b10970 / 03b10924 / 03b10a40`。

写前重新识别平台、查询全部选核并比较预期旧值。只有全部匹配，才进入逐核更新；
每个核心在写前再次查询，写命令成功必须得到本次 response 1，然后执行 A3 回读。
已经等于目标值的核心不发送设置命令。回读只接受目标 count 的精确有符号 32 位编码；
不把低 16 位碰巧相等、未知高位或其它原始返回解释成成功。

每次等待最多 100 次轮询，并受整个事务期限、取消标志和进程内互斥保护。
首个错误立即停止；保留每个核心的 attempted / accepted / verified / unchanged 状态，
以及有效的旧值和回读原值。批量更新不是原子操作，不自动重试、回滚、启用 OC 或修改其它核心。
外部工具不受这个进程内锁约束。

## GUI

AMD tuning 中填写 firmware CCD 与 cores，点击 **Read curve**，输入 count，
点击 **Apply curve to selected cores…**。确认框显示实际目标与数值。
更换 CPU、PCI、profile、CCD 或 core 后旧快照失效；确认期间变更目标会阻止提交；
异步读完成时不会发布已换目标的旧结果。提交后需重新读取才能再次编辑。
结果逐核区分回读匹配、未改变、固件接受但回读未确认、已尝试但未确认、尚未发送设置命令。

## CLI

```sh
octool-cli amd-curve-read --cpu 0 --ccd 3 --cores 0,2,7
```

根据刚读出的各核 `raw` 依升序填入预期值；以下只是三个旧值均为 `0xffffffe2` 的语法示例：

```sh
octool-cli amd-curve-set --cpu 0 --ccd 3 --cores 0,2,7 \
  --margin -25 --expect-raw 0xffffffe2,0xffffffe2,0xffffffe2 --apply
```

单核可用 `--core 7`；它与 `--cores` 互斥。所有语法、范围、旧值数量、重复核心和
`--apply` 检查发生在打开后端之前。旧值必须是 `-50..50` 的有符号 32 位原始编码。
JSON 包含 `phase`、`stopped_core`、各阶段 mask 和每核记录；顶层失败时可能已有部分核心完成。
`verified` 仅表示固件数值回读匹配，`hardware_effect_measured` 和
`physical_mapping_verified` 均为 false。多核读取任一失败时不发布部分快照。

## REA 与原指令依据

[REA 云端任务](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39435187923)
使用 REA 6.7.0 / Ghidra 12.1.4，完成 12 个函数的伪代码与函数身份检查；工具记录耗时 651.04 秒。
[哈希、映射和调用链](validation/amd-percore-rea-study.json)固定原 ELF SHA256、工作流提交和每个产物。
Ghidra 对本 ELF 增加 `0x100000` 基址，混合调用约定会错误显示 PCI 参数，故关键参数继续核对原始指令。

[可复现原指令实验](../analysis/tools/legacy-amd-curve-set.py)与
[固定片段](../analysis/fixtures/legacy-amd-curve-set.json)、[本机记录](validation/legacy-amd-curve-set-local.json)覆盖 80 组 setter 输入、
12 个访问失败位置、9 组偏移查询返回。对象、PCI、锁、延迟和 MP1 边界均为替身，未执行硬件访问。
原 setter 连续发送 D5 与 06/07，重复写入参数三次，不处理 PCI 写失败，且在最终读响应前解锁。
例如原负数 `-25`、CCD 3/core 7 被编码为 `3f7fffe7`，包含 CCX/保留位；新实现为 `3070ffe7`。
新实现没有照搬这套多命令流程，也未开放 D5、07、A8 或隐含 `50200=1`。

命令号、邮箱和低 16 位 count 约束与固定提交的
[Shimada 设置表](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)、
[逐核设置实现](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetPsmMarginSingleCore.cs)
交叉核对。这是软件协议证据，不能替代真机对端口、固件响应、核心映射和效果的验收。

## 逐核 / CCD 监控仍待确认

`get_percore_power_offset()` 实际调用 MP1 A8，忽略调用状态后返回输出字；不是可直接用的常量。
原调参页在 `GLOBAL_IS_SHIMADA` 非零时调用它并显示返回字，确认了平台分支，
但尚未证明独立 MP1 初始化、成功响应或该返回字与完整 PM 表布局的契约。
直接调用审计确认调参页 `cpufunctions` 使用默认 PM 构造器，逐核校准路径还会使用 `AMD_PM_LOG(int)`。
后者包含不同的起始偏移和计数分支，不能替换默认构造器加 `get_pm_patch` 的布局。
现有固定偏移与八种 PM 表长度的冲突仍未解决，因此没有用越界裁剪或算术外推启用逐核/CCD 功耗解码。
自动核心映射、其它 AMD 平台、高层 PBO/Curve Shaper、原生 PM 传输及板级调参仍未完成。
Fmax 与 core01 rails 的额外状态写入和连续命令见[后续入口研究](amd-pbo-setter-research.md)。
