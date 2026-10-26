# Shimada 全核 Curve Optimizer

GUI 与 CLI 共用 `setShimadaAllCoreCurve`，支持 AMD family `1Ah` / model `08h`、
PCI `1022:153a`、域 0 的 `00:00.0`。输入是整数 CO 计数 `-50..50`，不是电压。
这是固件定义的全核范围，会覆盖原有逐核曲线设置；不由软件推断启用核心列表。

CLI 先用 `amd-pbo-capabilities --cpu N` 读取完整能力字，再显式提交：

```sh
octool-cli amd-pbo-capabilities --cpu 0
# 仅当本次读取的能力字确实为 0x00000006：
octool-cli amd-curve-all-set --cpu 0 --margin -25 --expect-caps 0x00000006 --apply
```

示例值只展示语法，不是推荐调参。必须使用实际读取的能力字；未知位也参与比较。
GUI 在 AMD tuning 的全核曲线行输入计数，点击 Read capability 后再 Apply curve to all cores。
确认界面列出逻辑 CPU、全核范围和计数；输入变化、完成或失败后均需重新读取能力。

应用层要求能力 bit 2 为 1、手动 OC bit 0 为 0。这是本程序的保守前置条件，
不是对所有固件接受条件的声明。运行时重新读取能力，比较完整字，再复核 CPU/PCI 身份，
仅发送一次 RSMU `07`，arg0 为计数的低 16 位，arg1..5 为零。
不启用 OC，不改 VID/rails，不运行 Force/UnForce，不切换邮箱，也不自动重试或回滚。
零计数仍会提交设置，不能当成“当前已为零”的无操作。

返回 `accepted=true` 仅表示固件返回 `1`。没有全核一致回读合同，
因此 `verified=false`、`current_margin_counts=null`、`affected_core_count=null`、
`physical_mapping_verified=false` 始终保留。
硬件访问失败或取消发生在命令提交以后，设置可能已改变；GUI/CLI 保留 attempted/accepted 与错误状态。
需要精确比较与回读明确核心时，继续使用[逐核 / CCD 选核曲线接口](amd-curve-settings.md)。

消息与参数独立核对固定版本 ZenStates-Core：
[Shimada 设置](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)、
[全核命令](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetPsmMarginAllCores.cs)、
[参数转换](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Utils.cs)。
只采用该型号的现代 RSMU 路由，不沿用其它型号的 VID 替代或 MP1 路由。

逐核/CCD PM 布局、双 rails 硬件写入、动态内存/EC、其它平台和真机验收仍未完成。
本功能不能用来证明上述功能已恢复。
