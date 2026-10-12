# Intel 刷新、自刷新与 ZQ 参数：验证交付

生产提交 `97c1b22f87f3c8154aded5975833e5b40c61e516`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/intel-refresh-ci-97c1b22.json)绑定同一源码的云端任务、日志、截图、安装包和 SHA256。

GUI `Intel Controls → Memory timings` 与 CLI `intel-memory-read/decode` 同步增加
[14 个刷新、自刷新与 ZQ 参数](intel-memory-refresh.md)，当前共 18 寄存器 / 57 字段。
保留 DDR4/DDR5 别名及每字段单位；手册未明确单位的 TXSDLL / TSR 显示编码。
限定 B7/A700，读取已经启用的 MCHBAR，不增加设置、校准触发或训练命令。

新读取使用 v3，完整保留 v1（12 / 30）和 v2（14 / 43）快照的导入、保存与离线解码。
未知版本、缺失/重复寄存器、错误位宽和整字全 1 均拒绝。新增配置代码包括零，
不把推荐值当作读数，也不根据这些代码宣称配置有效或训练成功。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38208752141) | 4/4；各 15 CTest、226 场景组，含 23 组 Intel 内存、12 组 Intel 时钟、61 组 SPD |
| ASan / UBSan | SPD、PM、UMC、Intel 内存和 Intel 时钟快照共 5 项通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38208752189) | 12/12；十目标各 17 CTest、23 组命令场景、462 份 JSON，含 40 份 Intel 内存报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38208752202) | 23/23；十目标及 EL8 基线各 93 Qt 测试、11 套目标内核 VM |
| 既有分析回归 | 74 项通过 |
| 用户原目录本机 | 48 项：36 通过、12 因本机缺工具链跳过；文档、确定性源码归档和归档内容检查通过 |

十套 CLI 在无桌面、无 Qt、无构建工具的独立环境完成安装、普通用户启动、卸载和重装。
归档 580 张截图；人工检查页面及哈希见证据中的 `visual_review`。
24 个本轮文件同步原目录，18 个换行规范化后与生产提交相同；六份历史文档仅应用本轮变更。
12 个运行时、测试和构建文件全部匹配。驱动 / HAL / ABI 沿用此前实现。

## 交付文件

`dist/gui-cli-intel-refresh-97c1b22/` 包含 10 GUI、10 CLI、10 可选 DKMS 包，源码、
源码 SHA256 侧车和 `SHA256SUMS`，共 33 个文件。版号仍为 2.0.1，以提交目录及哈希区分。
源码 4,473,905 字节，SHA256 `926477a0cab86f563424d98559ad78c198e8a7edd2fd45b8946371edeb9ff8b1`。
包和源码固定于生产提交；最终验证记录由其后的文档提交补充。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 11,193,612 | 295,504 | 19,068 |
| el9 | 10,842,900 | 292,040 | 19,059 |
| el10 | 10,842,896 | 320,408 | 19,163 |
| ubuntu20.04 | 14,790,132 | 349,142 | 10,670 |
| ubuntu22.04 | 14,790,138 | 343,342 | 10,662 |
| ubuntu24.04 | 14,790,108 | 380,188 | 10,670 |
| ubuntu26.04 | 14,790,126 | 391,246 | 10,670 |
| debian11 | 14,790,120 | 342,066 | 10,670 |
| debian12 | 14,790,126 | 358,682 | 10,666 |
| debian13 | 14,790,124 | 371,286 | 10,672 |

## 验证范围与缺口

覆盖全部 51 个硬件读取失败位置、3 次 CPUID 失败、控制器与通道路由、18 个原始字
在两次采样间的变化、新字段完整编码范围、零配置、保留位、DDR4/DDR5 别名、Gear 覆盖、
高于 2^53 的 64 位原值、三版本隔离、伪造派生数值重新解码、取消及 GUI 异步生命周期。
旧版 BCLK / MRC、RTL、掉电、AMD 和 SPD 回归仍运行。

两次一致读不保证原子性；合成设备和虚拟机测试不构成目标真机验收。
原版全部功能尚未还原：[状态清单](recovery-status.md)仍包括 AMD PStates 设置、完整 PBO / 曲线
与 PM 表、Intel 其它型号和逐核 / TVB、剩余时序 / IOL / 实时内存频率、训练命令及板级
PMIC / VRM / EC / 时钟写入。Secure Boot 与调参稳定性也未通过真机验收。
Windows/macOS 核心通过不表示已完成这两套系统的硬件移植。
