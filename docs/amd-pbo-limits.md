# Shimada PBO 功率、电流与温度限制

GUI 和 CLI 共用核心新增 PPT、TDC、EDC、温度上限的单项设置。
限定 AMD family `1Ah` / model `08h`，PCI `1022:153a`，domain 0 / BDF 0。
这是命令提交与固件响应确认；当前限制回读及真实硬件效果尚未验证。

| 项目 | CLI `--limit` | 输入单位 / 应用范围 | RSMU 消息 | 参数 |
|---|---|---|---|---|
| PPT | `ppt` | 整数 1..2000 W | `56h` | 输入 × 1000 |
| VDD TDC | `tdc` | 整数 1..2000 A | `57h` | 输入 × 1000 |
| VDD EDC | `edc` | 整数 1..2000 A | `58h` | 输入 × 1000 |
| 温度上限 | `temperature` | 整数 1..100 °C | `59h` | 输入原值 |

这些范围是应用输入保护，不是推荐运行值，也不是芯片熔丝范围或主板能力承诺。
不接受零值恢复默认、负数、浮点、无符号回绕、SOC 电流或多项隐式设置。
设置会影响共享封装；选择逻辑 CPU 不等于选择某个物理核心的限制。

## 使用

```sh
octool-cli amd-pbo-capabilities --cpu 0
# 将查询得到的完整 capabilities_raw 传给 --expect-caps。
# 下例仅演示语法；选择适合自己硬件的限制。
octool-cli amd-pbo-limit-set --cpu 0 --limit ppt --value 350 --expect-caps 0x6 --apply
octool-cli amd-pbo-limit-set --cpu 0 --limit temperature --value 90 --expect-caps 0x6 --apply
```

GUI：AMD tuning → Read PBO capability → 选择 PPT/TDC/EDC/Temperature → 输入数值 → Apply limit。
每次应用后需重新读取能力。确认窗口期间改变目标、项目或数值时终止提交；
异步查询期间改变目标时丢弃旧结果。命令结果列出请求值、固件响应及回读不可用状态。

能力消息 `6Fh` 的 bit 1 必须置位（PowerLimitsAvailable），bit 0 必须清零（manual OC disabled）。
bit 2 是 PBO/Fmax 能力，单独置位不表示上述限制可用。
提交前重新查询并比较整个能力字，包括未知位，再重新验证 CPU/PCI 身份。
六个邮箱参数均初始化，argument 0 之外全部写零；仅发送所选的一条设置消息。
协议是 B8/BC RSMU `03B10924/03B10970/03B10A40`，不是原 BIOS SMUIO 或 MP1 通道。

`accepted=true` 仅表示读到固件响应 1。`verified=false`、`current_limit=null`、
`readback_supported=false` 与 `hardware_effect_measured=false` 始终保留。
命令发出后发生超时、取消或 I/O 失败时，设置可能已经生效；不自动重试、回滚、切换 OC 模式，
也不以参数回显、熔丝值或未经核对的 PM 偏移冒充当前限制。

## 原版与公开实现的交叉核对

[原版六字段研究](amd-limits-recovery.md)已用原指令建立 PPT/TDC/EDC/THM 的 UI 绑定、
乘法和分支。Shimada 单独匹配 `1022:153a` 不会设置该界面的第二标志：
该路径会选择旧 `53h–56h`，不能照搬到本平台。原槽即使返回失败仍显示 Applied，亦未沿用。

本次继续通过 [REA 云端任务](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39545928793)
分析十个原函数，包含六字段槽、动态 EDC 读取/更新/应用、三个测试消息入口、动态内存及温度读取。
REA 伪代码须与完整原指令交叉核对，不等同于固件规范或实际效果。
动态内存与 EC 动态 EDC 的 311 个原指令实验见[详细研究](amd-pbo-dynamic-research.md)，
包含忽略写入失败、16 位截断以及在刷新前显示 Applied 的行为；尚未开放这两条硬件路径。

命令号与单位转换依据固定的公开实现：

- [ZenStates Shimada 设置表](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)。
- [SetSmuLimit](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetSmuLimit.cs)、[SetTctlMax](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetTctlMax.cs)：前三项乘 1000，TYPE_CPU4 温度输入不缩放。未复制其 MP1 失败回退。
- [Linux corecycler 命令配置](https://github.com/Daaboulex/linux-corecycler/blob/afa82087132784d840078cfa26b5794a776cb1c6/src/corecycler/smu/commands.py)、[输入及单位](https://github.com/Daaboulex/linux-corecycler/blob/afa82087132784d840078cfa26b5794a776cb1c6/src/corecycler/smu/driver.py)提供另一份 W/A 接口对照；其命令来源也引用 ZenStates，不算独立真机验证。

固定 Shimada 表未配置当前限制查询。通用 GetPboFusedLimits / GetSystemConfiguredPowerLimit 类的存在
不构成本平台已支持的读回协议。逐核/CCD PM 解码、rails 写入、动态 EDC 自动策略与其它平台仍未完成。

## 验证范围

核心回归覆盖四个命令的独立参数、全部应用输入编码、能力变更、平台拒绝、失败响应、
逐访问故障/取消、超时，以及禁止旧命令/OC/MP1 回退。固件替身丢弃写入但返回成功时，
GUI/CLI 仍只报告接受，绝不标为已验证。CLI 在打开后端前拒绝不完整或越界输入。
GUI 验证四种设置、确认窗口取消/输入变化、陈旧能力、异步目标变化及固件拒绝。
这些合成验证不替代目标真机验收。
