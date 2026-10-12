# Intel TVB 配置恢复

2026-10-12 后续扩展：同一配置页和 CLI 已增加 [Ring DownBin](intel-ring-downbin.md)，页面名为 `TVB & Ring DownBin`。下面保留原两项 TVB 开关的恢复依据；本轮验证结果另行记录。

GUI `Intel Controls → TVB configuration` 和 CLI 共用同一事务核心，恢复 Raptor Lake-S
两个配置开关：高温倍频裁剪（Ratio clipping）和随温度变化的电压优化（Voltage optimization）。
当前仅允许 Intel Family 6 / Model `B7`、非虚拟机、具备 MSR 功能的目标 CPU。
固件必须成功响应读取命令；写入还要求 OC lock 未设置。

## 使用

先从 `octool-cli diagnose` 的 `allowed_cpus` 中选择逻辑 CPU。以下以 0 为例：

```sh
octool-cli intel-tvb-read --cpu 0
octool-cli intel-tvb-set --cpu 0 --field ratio-clipping --value 1 --apply
octool-cli intel-tvb-set --cpu 0 --field voltage-optimization --value 1 --apply
```

`--value 1` 表示启用，`0` 表示禁用。命令示例说明接口，不代表对具体 CPU 的配置建议。
CPU 用于定位共享处理器配置；这不是对单独物理核心的覆盖。
GUI 同样先读取、再选择一个开关及目标状态，确认后应用。更换 CPU、读取失败、写入未验证时会清空旧值。
两端均区分“设置写入已尝试”“配置回读验证通过”“原值已相同、未写设置”。

## 依据和协议

[Intel 发布的 Raptor Lake-S FSP 头文件](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h)
定义了这两个独立配置项及其含义。其中 `0x02A0/0x02A1` 是 **FSP 配置结构偏移**，不能作为硬件地址。
FSP 文件没有给出本实现使用的邮箱命令与位域；后者来自原版 ELF 的完整函数反汇编和隔离仿真。

| 原版函数 | 地址 / 字节数 | 已确认行为 |
|---|---|---|
| `set_tvb_clip(bool)` | `0x3737F0` / 234 | `true` 清除 data bit 2，`false` 设置 bit 2 |
| `disable_volt_opt(bool)` | `0x37FE50` / 189 | `true` 设置 data bit 3，`false` 清除 bit 3 |
| `Rd_Misc_Ratio()` | `0x395240` / 340 | 返回 data bits 1、2、3、4 的状态 |

MSR `0x150`，查询命令 `0x18`，设置命令 `0x19`；本功能的参数域均为零。
两个设置都以 **disable bit** 编码，对外接口统一为 enabled，避免把原版函数参数方向混用。
保留其余 31 位，包括 Ring DownBin、Vmax 和未知位。此次不提供这些位的设置入口。

原版 ELF SHA256：`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
完整函数字节：[固定 fixture](../analysis/fixtures/legacy-intel-tvb.json)；
[静态证据](validation/legacy-intel-tvb-static.json)、[47 场景仿真](validation/legacy-intel-tvb-emulation.json)、
[来源和边界](validation/intel-tvb-sources.json)。仿真只运行白名单函数字节，MSR、容器和延时边界全部为合成实现。

## 事务规则

1. 核验 CPU、虚拟机标志、MSR 功能并读取 `MSR_FLEX_RATIO[20]` OC lock。
2. 查询前和提交后轮询 busy，每段最多 100 次，且受整个事务的取消信号和截止时间约束。
3. 接受完整 8 位固件状态码为零的完成样本；不额外读取一个可能已变化的“完成后”样本。
4. 设置前重新核验身份、锁寄存器和整个旧配置。任何位变化都拒绝旧快照。
5. 只改变选定的一位；相同值跳过 `0x19`；提交设置后重新查询 `0x18` 并核验完整配置及身份/锁上下文。
6. 发生失败后不重试设置、不自动回滚。JSON 的无效配置字段为 null，GUI 不展示缓存值。

原版查询没有检查 busy/错误码，部分 setter 没有等待完成，且没有独立回读。
本次保留已确认的命令和位域，错误处理采用共享核心的有界事务规则。
[Intel 贡献的 Mailbox 接口定义](https://github.com/tianocore/edk2-platforms/blob/7f53d84a042e1f30bbaee462cf636326a4f76054/Silicon/Intel/CoffeelakeSiliconPkg/Cpu/Include/Library/CpuMailboxLib.h)
也区分了 busy 超时、传输结果和固件命令状态。

## 范围限制

成功的邮箱应答与配置回读不能证明某个 SKU 正在执行 TVB，不能证明频率、电压或性能达到预期。
不推导 SKU 的 TVB 支持位，不将原版 `octvb_support()` 对单个错误位的判断当作能力枚举。
多个程序同时操作硬件仍可能产生竞争；用户态串行事务不等于系统范围原子操作。

TVB 完整写入范围、逐物理核心裁剪表、Vmax stress、Ring DownBin、增强 TVB、其它客户端/服务器型号
尚未由此功能恢复。W790/W890、AMD P-state 写入、完整 AMD MP1/PBO/目标映射、板级调节等原有缺口也仍存在。
合成测试、跨发行版安装和虚拟机驱动验证均不替代目标真机调节验收。

活动 P 核候选表的两组温度 / 降倍率查询已由后续 [TVB thresholds](intel-tvb-table.md) 页面和 CLI 同步恢复。

保守输入范围内的 TVB 分档单字段设置已由后续 [编辑功能](intel-tvb-table-edit.md)同步恢复到 GUI / CLI。
