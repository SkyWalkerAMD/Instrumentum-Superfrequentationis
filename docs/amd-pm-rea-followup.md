# REA / Ghidra 辅助分析后续记录

2026-10-20。最新运行取得 **5 个目标函数的伪代码**，文件偏移、分析地址和名称检查通过。
功能代码的新增 27 项监控由独立原指令回放验证，见[实现范围](amd-pm-fabric.md)；
REA 研究不计入 GUI / CLI 的 43 项功能验证任务。

本二进制已跑通的工作流输入为 `startup_timeout_ms=900000`，
`disabled_analyzers=GCC Exception Handlers,Decompiler Switch Analysis,Call Convention ID`。
应显式传入这两个研究参数；工作流默认值仍保留通用配置。
首次查询含导入与分析，耗时 697.56 秒，后续四个函数约 11.91 秒。

## 执行记录

沿用 [REA](https://github.com/morluto/rea) 6.7.0、Ghidra 12.1.4、Node 24.11.1、
Temurin 21 和 4 GiB Java 堆。原 ELF 摘要、依赖固定值、运行身份与逐文件 SHA256
见[机器记录](validation/amd-pm-rea-followup.json)。原版程序没有执行，未加载硬件驱动。

| 运行 | 关闭的自动分析器 | 伪代码查询耗时（秒） | 结果 |
|---|---|---:|---|
| [39417148443](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39417148443) | 默认 | 121.28 | 超时，未取得伪代码 |
| [39417696591](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39417696591) | GCC Exception Handlers | 600.98 | 超时，未取得伪代码 |
| [39419160257](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39419160257) | GCC Exception Handlers,Decompiler Switch Analysis | 900.86 | 超时，未取得伪代码 |
| [39420888286](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39420888286) | — | — | 取消：调整配置后另启运行 |
| [39421426795](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39421426795) | GCC Exception Handlers,Decompiler Switch Analysis,Call Convention ID | 709.47 | 5 个目标函数已返回并通过偏移/入口/名称检查 |

首次短期限重试验证了异常路径导出功能：外层响应过大时，仍能保存原始错误。
日志包含大量异常表处理错误；跳过 GCC 异常处理后，该错误洪流消失，但整文件分析仍超时。
继续跳过开关分支分析后仍有大量无关函数的反编译警告，最后再跳过全程序调用约定推断。
这些是逐步缩减分析范围的实验，不能据此认定只有一个根因或发生内存不足。
第二、第三次导出的证据包为空；完整失败诊断保存在相应 `pseudo-*.json` 响应内。
日志本身的保留上限仍然有效，不宣称保存了 Ghidra 的无限完整输出。

## 函数身份与范围

原文件为 PIE。所选五个函数的 ELF 虚拟地址恰好等于文件偏移；
分析器可以给整个映像加基址。调用器使用 `file-offset:0x...`，要求映射地址等于
函数入口，再查询该地址的函数名称。合成检查验证正确重定位可用，并拒绝落在函数内部、
错误文件偏移、缺少/重复映射以及空伪代码等六种情况。

目标为 `AMD_PM_LOG::get_pm_patch`、构造函数、`Wr_Scratch2`、
`get_fused_ccd` 与 `get_amd_native_thread_count`。返回的 C 类型、调用约定与完整
函数范围仍需与原指令比较；异常路径、开关恢复、间接调用与物理目标没有因查询成功而认证。
原始结果保留在用户目录 `build/rea-*/`，不并入源码压缩包。

实际对照发现五个分析地址均增加 `100000h` 基址。
`get_pm_patch` 的原符号长 22,281 字节，Ghidra 函数体记录 21,923 字节；
构造函数原符号长 36,961 字节，函数体记录 34,284 字节。
差额包括填充和未收录的异常相关尾部；查询成功不代表完整函数覆盖。

还有一处明确的参数推断问题：`Wr_Scratch2` 原指令通过 ECX/EDX/R8D 传递 PCI 配置调用参数，
并保留原输入；返回伪代码却显示未初始化的 `in_ESI` 和由延时值衍生的参数。
`get_amd_native_thread_count` 的返回类型也被推断为 void。
这些结果适合导航控制流程，传输参数必须继续依据原指令核对，不能直接移植伪代码。
[指令对照记录](validation/amd-pm-rea-instruction-crosscheck.json)保留函数范围、原始机器码及观察。

分析器名称和行为核对自固定版本官方源码：
[GCC Exception Handlers](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Features/Base/src/main/java/ghidra/app/plugin/exceptionhandlers/gcc/GccExceptionAnalyzer.java)、
[Decompiler Switch Analysis](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Features/Decompiler/src/main/java/ghidra/app/plugin/core/analysis/DecompilerSwitchAnalyzer.java)、
[Call Convention ID](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Features/Decompiler/src/main/java/ghidra/app/plugin/core/analysis/DecompilerCallConventionAnalyzer.java)。
REA 的文件偏移选择契约还按固定 npm 包内的 Java 桥与结果解析代码核对。

后续继续从这些函数追踪逐核/CCD 布局、校准与传输边界，
并核对 [GMI、DIMM、PCIe 数组](amd-pm-array-research.md)的原界面选择方式；
没有据此启用新的硬件写入。
