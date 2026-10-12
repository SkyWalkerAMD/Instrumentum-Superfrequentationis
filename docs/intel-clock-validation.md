# Intel 时钟信息：验证交付

生产提交 `b3a40d7e8895b363f19032a1d852911846d11269`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/intel-clock-ci-b3a40d7.json)绑定相同源码的云端任务、日志、截图、安装包和 SHA256。

GUI `Intel Controls → Clock information` 与 CLI `intel-clock-read/decode`
同步提供 [BCLK 报告和最后一次 MRC 请求](intel-clock.md)：BCLK kHz、Gear/参考时钟/QCLK
比率、VDDQ TX 电压请求及 IccMax 编码。限定 B7/A700，读取三个共享 PCU 寄存器。
MC_BIOS_DATA 明确标为历史训练请求，不显示成当前内存频率、实时 Gear 或实测电压。
IccMax 的官方描述与位宽冲突，因此只显示编码。全部为读取功能。

新增独立 `octool-intel-clock-v1` 快照；GUI 与 CLI 可双向导入、从原始字重新解码。
未知字段显示不可用；读取失败、忙或两次数据不一致时不发布部分捕获或旧结果。
已有 Intel 时序 v1/v2 不变，共用提取后的 CPU/PCI/MCHBAR 检查。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38200818742) | 4/4；各 15 CTest、221 场景组，其中 12 组 Intel 时钟、18 组 Intel 内存、61 组 SPD |
| ASan / UBSan | SPD、PM、UMC、Intel 内存与 Intel 时钟快照共 5 项通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38200822292) | 12/12；十目标各 17 CTest、22 组命令场景、450 份 JSON，其中 20 份 Intel 时钟报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38200800678) | 23/23；十目标及 EL8 基线各 91 Qt 测试、11 套目标内核 VM |
| 既有分析回归 | 74 项通过 |
| 用户原目录本机 | 48 项：36 通过、12 因本机缺工具链跳过；文档和确定性源码归档检查通过 |

十套 CLI 在无桌面、无 Qt、无构建工具的独立环境完成安装、普通用户启动、卸载和重装。
归档 570 张截图；人工检查的页面、观察结果和哈希见 `visual_review`。
32 个本轮文件同步原目录，26 个换行规范化后与生产提交一致；六份既有文档仅应用本轮变更，
保留独立历史。21 个运行时/测试/构建文件全部匹配。本轮没有修改驱动/HAL/ABI。

## 文件

`dist/gui-cli-intel-clock-b3a40d7/` 包含 10 GUI、10 CLI、10 可选 DKMS 包，源码、
源码校验侧车和 `SHA256SUMS`，共 33 个文件。版号仍为 2.0.1，以提交目录和哈希区分。
源码 4,434,378 字节，SHA256 `f06f3e9097174e321caba120e641a877a7a964880a0f126c3a5c817208be99e9`。
包和源码固定于生产提交；最终验证记录属于其后的文档提交。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 11,164,140 | 293,096 | 19,068 |
| el9 | 10,799,913 | 289,685 | 19,054 |
| el10 | 10,802,057 | 318,706 | 19,164 |
| ubuntu20.04 | 14,755,084 | 346,250 | 10,668 |
| ubuntu22.04 | 14,755,058 | 340,376 | 10,670 |
| ubuntu24.04 | 14,755,100 | 377,734 | 10,674 |
| ubuntu26.04 | 14,754,978 | 388,970 | 10,670 |
| debian11 | 14,754,984 | 339,050 | 10,668 |
| debian12 | 14,755,084 | 355,616 | 10,668 |
| debian13 | 14,755,082 | 368,866 | 10,670 |

## 范围

来源为 Intel 743846-001 §3.3.42、3.3.43、3.3.51，见[来源记录](validation/intel-clock-sources.json)。
回归覆盖全部 21 个硬件读取故障位置、3 次 CPUID 失败、忙状态、保留编码、基址和数据变化、
取消、64 位 JSON 精度、离线无硬件访问、GUI 清理和异步生命周期。
合成设备测试不构成真机验收；两次匹配不能保证硬件原子性。

原版全部功能仍未还原。[状态清单](recovery-status.md)保留实时内存频率、其它型号时序/IOL、
训练与 BCLK 写入、AMD PStates 写入、完整曲线/PBO/PM 表、Intel server 域/逐核 override/TVB，
以及板级 PMIC/VRM/EC/时钟协议。四台目标真机、调参稳定性与 Secure Boot 验收尚未完成；
Windows/macOS 核心通过不表示已完成对应系统的硬件移植。
