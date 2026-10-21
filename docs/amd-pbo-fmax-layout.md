# Fmax 命令、rails 编码与逐核/CCD 布局诊断

本轮 GUI/CLI 共用 `amd_pbo` 和 `amd_pm` 核心。Fmax 恢复的是固件命令提交及完成状态检查，
**没有当前 Fmax 回读**。Rails 只有离线编码预览；逐核/CCD 只有布局诊断，尚未启用真实遥测解码。

## Fmax

限定 AMD family 1Ah / model 08h，PCI 1022:153a、0000:00:00.0。
先用 RSMU `6Fh` 读取能力字：bit 2 表示 PBO 可用，bit 0 表示手动 OC 已启用。
只有 PBO 可用且手动 OC 关闭时允许提交。完整能力字与用户提供的旧值比较，保留未知位；
变化返回 `EAGAIN`，不发送设置。提交前再次核验 CPU 和 PCI 身份。

Fmax 用 RSMU `70h`，argument 0 为整数 MHz，argument 1–5 清零。
输入 1–1048575 是二十位**编码范围**，不是推荐或经硬件验证的工作范围。
独立的 [ZenStates 设置表](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen5Settings_ShimadaPeak.cs)
与 [SetBoostLimitAllCore](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetBoostLimitAllCore.cs)
交叉支持命令和参数编码；原版 `set_fmax` 构造/调用证据见 [PBO 研究](amd-pbo-setter-research.md)。
能力位依据固定版本 [Cpu.cs](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Cpu.cs)。

传输先等待邮箱空闲，清 response、写六个参数、提交并轮询完成。response 为 1 仅标记 `accepted=true`；
`verified` 始终为 false，`current_fmax_mhz` 始终为 null。
`phase=complete` 只表示命令事务结束。BIOS、固件策略或系统可能使实际行为不同。
没有自动 OC enable、`50200=1`、MP1 回退、重试或回滚，也不把熔丝上限当成当前设置。
服务内事务不排斥其它软件或固件并发访问。

```sh
octool-cli amd-pbo-capabilities --cpu 0
# 将实际读取的完整 capabilities_raw 作为 --expect-caps；不要照抄示例值。
octool-cli amd-pbo-fmax-set --cpu 0 --mhz 5000 --expect-caps ACTUAL_RAW --apply
```

GUI 的“Read PBO capability / Apply Fmax”使用同一实现。目标或确认中的输入变化会撤销此次提交；
每次操作后重新读取能力。界面明确显示固件接受与实际值未验证两种不同状态。

## Rails

原版每字段编码为 `(mV-245)/5`，低 9 位；第二字段左移 17 位。
两输入为零表示不提交；单个零复制另一输入。固定
[Utils.cs](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Utils.cs)
支持 SVI3 的 245 mV 基点和 5 mV 步长，但其辅助函数只保留八位，**不足以认证原版双字段协议**。
尤其不能据此认定两字段就是特定 CCD/物理电压轨。

新离线预览要求非零输入 250–2800 mV 且为精确 5 mV 步长，避免原版除法截断与九位回绕。
该范围也仅代表编码能力，不表示适用电压。GUI 不提供发送按钮，CLI 不接受 `--apply`：

```sh
octool-cli amd-pbo-rails-encode --rail0-mv 700 --rail1-mv 1250
```

## 逐核/CCD 布局

`auditLegacyAmdPmLayout` 独立复现原版**默认构造入口**及数组起点决策：
core 首字节为 `5F4h/634h`，CCD 为 `1F74h/2134h`，以 `730404h` 为分界；
原 fused CCD 输入不大于 2 时再加 `40h`。
1、2、4、8、10、12、14、16 输入对应同数 CCD 槽位及八倍 core 槽位；其余 1–16 内输入回落到 16/2。
输入为用户明确给出的原构造参数，不从版本中间字节或 Linux 逻辑 CPU 数猜测。

诊断列出 15 种 core 行、11 种 CCD 行的首/尾字节、槽数、超出捕获长度的字数、
core/CCD 重叠字数及 `714h/71Ch` 公共字段别名。
730204/2844 字节/输入 2 有 22 个 CCD 字越界；731004/11636/输入 16 有 176 个 core/CCD 别名字。
范围是诊断结果；不会裁剪数组、补零、扩大读取长度或显示伪造功耗。

```sh
octool-cli amd-pm-layout-audit --version 0x730204 --bytes 2844 --fused-ccds 2
```

此命令完全离线，不打开后端或文件。GUI 打开/读取 PM 表后可输入同一参数，审查并复制独立 JSON 诊断。
它检查实际捕获长度；不改变已有 220 个字段的解码和捕获格式。
三参数构造、单参数构造和默认构造不能混为一种布局。

## 证据与限制

本轮实际运行 [REA 12 函数分析 39537603812](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39537603812)，
研究提交 `c25aecec614c338559bfb06863550e7f3555f117`；含替代构造、Fmax 熔丝读取、流式消费者和调参按钮。
原 ELF SHA256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
Windows `Tool.exe` 的静态候选函数也使用同样的固定起点，尚不能解决固件布局冲突。
反编译、原指令合成回放、软件回归均不替代真机验收。
