# Intel 内存时序：验证交付

生产提交 `001c80437995e3bc7f80f1a3bc3be7704c44bb87`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/intel-memory-ci-001c804.json)绑定相同源码的云端任务、日志、截图、包和 SHA256。

GUI 的 Intel Controls → Memory timings 与 CLI 同步新增
[Raptor Lake-S 内存时序读取](intel-memory.md)：30 个字段，包括 CL、CWL、RCD、RP、RAS、
WRPRE、RDPRE、FAW、RRD、RFC、REFI 与四组读写转换时序。
DDR4/DDR5 不适用字段为空；不把 WRPRE 当成 WR，不推算未确认的 MHz/ns。

固定读取 12 个寄存器，先检查型号、主桥、已启用的 MCHBAR、内存类型、DIMM 容量与 rank。
两组采样和其间主桥配置必须一致；失败清除旧结果，不发布半份时序。
GUI 可打开、复制、保存快照；CLI 可导出和离线导入同一格式，离线操作不创建硬件后端。
原始 64 位字使用十六进制字符串，导入时重新计算字段，避免 JSON 数值精度和伪造派生值问题。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38148549524) | 4/4；各 14 CTest、205 场景组，其中 14 组 Intel 内存、61 组 SPD |
| ASan / UBSan | SPD、PM、UMC、Intel 内存快照测试通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38148554697) | 12/12；十目标各 16 CTest、19 组命令场景、422 份 JSON，其中 20 份 Intel 内存报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38148404619) | 23/23；十目标及 EL8 基线各 84 Qt 测试、11 套目标内核 VM |
| 既有分析回归 | 74 项通过 |
| 用户原目录本机 | 48 项：36 通过、12 因本机缺工具链跳过；文档和确定性源码归档检查通过 |

旧提交 `1da2d92` 的核心任务通过，但完整矩阵被后续修正替代，不计入本次最终验收。
复核手册发现空通道的 rank 占用默认值也可能为 0Fh，因此新增 DIMM L/S 容量检查。
随后 `a156f1d` 的 Rocky Linux 10 界面测试发现固定截图高度不能容纳全部行；
测试改为先验证普通窗口可以滚动至末行，再根据实际行高调整截图窗口。
失败日志已保留，最终验收记录均对应 `001c804`。

十套 CLI 在无桌面、无 Qt、无构建工具的独立环境完成安装、普通用户启动、卸载和重装。
归档 540 张截图，人工检查 Ubuntu 24.04、EL8 和 EL10 的六张新页面截图；观察记录见 `visual_review`。
27 个本轮文件同步原目录，21 个换行规范化后与生产提交一致；六份既有文档仅应用本轮变更，
保留原目录独立历史。17 个运行时/测试/构建源文件全部匹配。本轮没有修改驱动/HAL/ABI。

## 文件

`dist/gui-cli-intel-memory-001c804/` 包含 10 GUI、10 CLI、10 可选 DKMS 包，源码、
源码校验侧车和 `SHA256SUMS`，共 33 个文件。版号仍为 2.0.1，以提交目录和哈希区分。
源码 4,356,324 字节，SHA256 `bec7bc6e855435e3c42ea510e7a01f6985fc198c5c1b1cac133537f0ff6e303b`。
包和源码固定于生产提交；最终验证记录属于其后的文档提交。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 11,090,648 | 277,776 | 19,072 |
| el9 | 10,735,524 | 275,787 | 19,058 |
| el10 | 10,734,553 | 297,848 | 19,161 |
| ubuntu20.04 | 14,653,968 | 327,880 | 10,672 |
| ubuntu22.04 | 14,653,932 | 321,440 | 10,670 |
| ubuntu24.04 | 14,653,996 | 348,554 | 10,670 |
| ubuntu26.04 | 14,653,988 | 359,252 | 10,670 |
| debian11 | 14,653,932 | 319,540 | 10,668 |
| debian12 | 14,653,940 | 332,482 | 10,670 |
| debian13 | 14,653,936 | 340,444 | 10,674 |

## 范围

根据 Intel 官方 743846-001 桌面手册，完整 PDF 已下载并保存 SHA256；关键表格经过图像核对。
逐项来源见[来源记录](validation/intel-memory-sources.json)。硬件读取仅开放 CPUID B7 / PCI A700，
DDR4 为每控制器 channel 0，DDR5 为 channel 0/1；索引不直接代表主板 A1/B2 插槽。
任一 Gear 除法覆盖标志开启时保留编码。两次一致读取不能保证原子性或训练稳定性。

原版全部功能仍未还原，[状态清单](recovery-status.md)保留其它 Intel 时序/RTL/IOL/频率和训练写入、
AMD PStates 写入、完整曲线/PBO/PM 表、Intel server 域/逐核 override/TVB，
以及板级 PMIC/VRM/EC/时钟协议。四台目标真机、调参稳定性与 Secure Boot 验收尚未完成；
Windows/macOS 核心通过不表示已完成对应系统的硬件移植。
