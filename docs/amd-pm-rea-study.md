# AMD PM：REA 辅助研究与原指令实验

2026-10-20：新增的 53 项公共功耗与策略监控已经通过独立的原指令回放及
[GUI / CLI 整批验证](amd-pm-package-validation.md)。以下研究不属于新增可用功能，
也不计入 43 项成功的功能验证任务。

## REA 云端运行的实际结果

采用用户提供的 [REA 项目](https://github.com/morluto/rea)，固定 npm
`rea-agents@6.7.0` 与 [Ghidra 12.1.4](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build)。
工具安装包经过摘要校验；Ubuntu 24.04 云端使用 Node 24.11.1、Temurin 21、
4 GiB Java 堆及 2,400,000 ms 启动期限。工作流为
`.github/workflows/rea-native-study.yml`，调用器为
`analysis/tools/rea-native-study.py`。没有执行原版 ELF 或加载驱动。
分析子进程使用隔离的用户目录和明确列出的环境变量，不继承 GitHub 令牌。

原 ELF SHA256 为
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
[运行 39391260244](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39391260244)
基于研究提交 `7c35efe`；功能验证提交为 `e2660cb`，两者用途不同。

| 操作 | 实际结果 |
|---|---|
| MCP 初始化、工具清单 | 成功，保留返回记录 |
| `open_binary` | 成功识别 x86_64 ELF 与上述 SHA256 |
| `procedure_pseudo_code(0x2e7360)` | 约 2401.66 秒后失败，未取得伪代码 |
| 其余四个函数、函数信息、调用者 | 因首项失败而未执行 |
| `close_binary` | 成功 |
| 云端任务 | 失败；错误记录已下载保留 |

首个查询原始错误类别为 `provider_timeout`。完整错误超过 MCP 响应预算，
REA 返回外层 `resource_constraint`；响应中保留原错误类别和证据 ID。
调用器在异常后直接关闭会话，**没有调用 `export_evidence_bundle` 导出完整错误**，
因此无法判断具体哪个 Ghidra 分析器耗时或失败，也不能将原因归结为内存不足。
本轮成功取得的原版函数伪代码数量为 **0**。

工具版本、安装包摘要、云端产物摘要、逐文件哈希及原始错误见
[机器记录](validation/amd-pm-rea-study.json)。
本地完整记录保留于 `build/rea-study-39391260244/`；源码包不包含 `build/`。
后续 REA 重试应先在异常路径导出保留证据，根据原始日志决定分析范围；
不应只延长超时时间并重复整文件分析。

## 独立原指令实验

这些实验使用 Unicorn 执行固定原版机器码；CPUID、MMIO、拓扑和 C++ 库边界
由测试程序提供合成结果。它们没有取得真机数据，也不是 REA 的反编译产物。

`FamilyType` 位于 `21E510h`，长 156 字节。对扩展家族字段 0–255 逐项回放，
其返回值如下；[完整 256 项记录](validation/amd-pm-family-probe.json)包含代码字节与摘要。

| 合成扩展家族字段 | 原函数返回值 |
|---|---|
| `06h` | `0Fh` |
| `08h` | `11h` |
| `0Ah` | `13h` |
| 其它，包括 `0Bh` | `0` |

因此，先前合成上下文中家族选择值为零，不能单独作为“忘记初始化”的证据。
这只说明该辅助函数的行为，尚不能证明整个构造过程、CPU 身份或固件路径已完整建模。

校准实验在两个表版本、两个合成 CCD 输入下，分别持续返回四种 MMIO 浮点位模式，
共 16 个场景；进入 `get_pm_patch(3)`，在 `2E73A2h` 或测试程序的
10,000 条指令预算处停止。[完整记录](validation/amd-pm-calibration-probe.json)列出
读取位置、次数及对象成员变化。

| 合成返回值 | 观察 |
|---|---|
| `5.0` | 返回后续解码入口；校准标志置位 |
| `0.0`、`8.0` | 在本次指令预算内未完成校准 |
| quiet NaN `7FC12345h` | 也返回后续解码入口，并将校准标志置位 |

10,000 条指令是实验预算，**不是原版超时**；这些场景不证明真机卡死。
全部 16 项均未改变本次观察的逐核计数成员 `68h–73h`。
校准完成也没有解释先前逐 CCD 固定起点与部分已知表长度之间的冲突。

## 对后续还原的影响

现有有界解码器继续拒绝非有限值，不复用原版扫描校准来绕过长度验证。
逐核/CCD 字段还需核对构造分支、GUI 标签选择、固件表布局与读取窗口；
它们尚未加入生产解码器。
本轮 53 项只确认原版名称、偏移及浮点存储值，物理单位、策略状态和目标仍未认证，
详见[实现说明](amd-pm-package.md)及[剩余功能清单](recovery-status.md)。
