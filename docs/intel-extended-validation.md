# Intel RTL 与掉电时序：验证交付

生产提交 `ec6077e05af4e793d22b2876ddd80b018be1c65a`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/intel-extended-ci-ec6077e.json)绑定相同源码的云端任务、日志、截图、安装包和 SHA256。

GUI 与 CLI 同步新增 [13 个 RTL / 掉电字段](intel-memory-extended.md)，共 43 项。
有效 Rank 的 RTL 使用 QCLK，CSL/CSH 使用 MC_DCLK，其余新增项使用 tCK；
Gear 除法覆盖时只有 tCK 字段退回 encoded。空 Rank 不显示 RTL，DDR4 tCPDED 允许零。
固定读取 14 个寄存器、比较两组完整采样，旧 v1 的 12 寄存器 / 30 字段仍可导入和导出。
当前限定 B7/A700，全部为读取功能，不恢复时序写入或训练命令。
后续[频率研究](intel-memory-frequency-research.md)确认 MC_BIOS_DATA 不能作为当前内存频率，
已记录请求编码和 BCLK 单位；这部分属于交付后的研究文档，未新增运行时访问。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38185638950) | 4/4；各 14 CTest、209 场景组，其中 18 组 Intel 内存、61 组 SPD |
| ASan / UBSan | SPD、PM、UMC、Intel 内存快照测试通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38185638958) | 12/12；十目标各 16 CTest、20 组命令场景、430 份 JSON，其中 28 份 Intel 内存报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38185638884) | 23/23；十目标及 EL8 基线各 86 Qt 测试、11 套目标内核 VM |
| 既有分析回归 | 74 项通过 |
| 用户原目录本机 | 48 项：36 通过、12 因本机缺工具链跳过；文档和确定性源码归档检查通过 |

十套 CLI 在无桌面、无 Qt、无构建工具的独立环境完成安装、普通用户启动、卸载和重装。
归档 540 张截图；实际人工检查的页面、观察结果和哈希见 `visual_review`。
19 个本轮文件同步原目录，13 个换行规范化后与生产提交一致；六份既有文档仅应用本轮变更，
保留独立历史。8 个运行时/测试源文件全部匹配。本轮没有修改驱动/HAL/ABI。

## 文件

`dist/gui-cli-intel-extended-ec6077e/` 包含 10 GUI、10 CLI、10 可选 DKMS 包，源码、
源码校验侧车和 `SHA256SUMS`，共 33 个文件。版号仍为 2.0.1，以提交目录和哈希区分。
源码 4,392,693 字节，SHA256 `5e8db57ca595a0d742538e38a8ec11baf0527aad7c4e2821597107d359ce1509`。
包和源码固定于生产提交；最终验证记录属于其后的文档提交。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 11,119,004 | 280,496 | 19,068 |
| el9 | 10,760,645 | 278,228 | 19,057 |
| el10 | 10,761,381 | 301,033 | 19,164 |
| ubuntu20.04 | 14,692,886 | 330,802 | 10,664 |
| ubuntu22.04 | 14,692,846 | 323,858 | 10,666 |
| ubuntu24.04 | 14,692,908 | 349,476 | 10,666 |
| ubuntu26.04 | 14,692,836 | 363,390 | 10,666 |
| debian11 | 14,692,838 | 321,816 | 10,664 |
| debian12 | 14,692,842 | 334,334 | 10,668 |
| debian13 | 14,692,898 | 343,776 | 10,662 |

## 范围

逐项依据见[来源记录](validation/intel-memory-sources.json)，新表格 149、152、153、155 页已图像核对。
回归包括 15 种非空 Rank 掩码、保留位、零值、v1/v2 版本切换和全部 43 个读取故障位置。
合成设备测试不构成真机验收；两次匹配不能保证硬件原子性或训练稳定性。

原版全部功能仍未还原。[状态清单](recovery-status.md)保留其它 Intel 型号 RTL、剩余时序/IOL/频率
和训练写入、AMD PStates 写入、完整曲线/PBO/PM 表、Intel server 域/逐核 override/TVB，
以及板级 PMIC/VRM/EC/时钟协议。四台目标真机、调参稳定性与 Secure Boot 验收尚未完成；
Windows/macOS 核心通过不表示已完成对应系统的硬件移植。
