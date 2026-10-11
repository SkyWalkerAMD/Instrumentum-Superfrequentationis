# DDR4 非对称容量：验证交付

生产提交 `57924bb34354e4db7bae36c58eeae6f3c01dc78c`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/ddr4-asymmetric-ci-57924bb.json)绑定相同源码的云端任务、日志、截图、包和 SHA256。

GUI / CLI 共用解码器，新增[已确认的 DDR4 非对称容量](ddr4-asymmetric.md)：
SDP / DDP 的三 rank 组合、同类封装组合，以及各自 2–8 层的两组 3DS。
两组分别使用自身密度与 rank / 堆叠因子，显示分项与总量。
密度比编码使用规范表中的离散等级，16 Gbit 后一级为 12 Gbit。
未确认封装、保留编码、未知版本或损坏 CRC 不生成猜测容量。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38143970745) | 4/4；各 13 CTest、191 场景组，其中 61 组 SPD |
| ASan / UBSan | SPD、PM、UMC 快照测试通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38143970568) | 12/12；十目标各 15 CTest、17 组命令场景、402 份 JSON，其中 101 份 SPD |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38143970549) | 23/23；十目标及 EL8 基线各 79 Qt 测试、11 套目标内核 VM |
| 既有分析回归 | 74 项通过 |
| 用户原目录本机 | 48 项：36 通过、12 因本机缺工具链跳过；文档和确定性源码归档检查通过 |

EL9 驱动任务第一次在下载容器镜像时遇到 Docker Hub 超时；保留失败日志并在相同源码提交
重新执行该任务。其余构建结果及源码没有被替换。最终结果均核对到上述生产提交。

十套 CLI 在无桌面、无 Qt、无构建工具的独立环境完成安装、普通用户启动、卸载和重装。
保存 510 张截图，人工检查 Ubuntu 24.04 与 EL8 的新 DDR4 容量页面；两个容量分项与
合计同屏可见。22 个本轮文件同步原目录，16 个换行规范化后与生产提交一致；六份既有
文档仅应用本轮变更，保留原目录独立历史。11 个运行时/测试/构建源文件全部匹配。
本轮没有修改驱动/HAL/ABI。

## 文件

`dist/gui-cli-ddr4-asymmetric-57924bb/` 包含 10 GUI、10 CLI、10 可选 DKMS 包，源码、
源码校验侧车和 `SHA256SUMS`，共 33 个文件。版号仍为 2.0.1，以提交目录和哈希区分。
源码 4,310,172 字节，SHA256 `bb5a20527ec924f0164180e2f0965c64fd33a11f03761ad93297fc5f902c8511`。
包和源码固定于生产提交；最终验证记录属于其后的文档提交。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 11,038,316 | 261,396 | 19,068 |
| el9 | 10,689,214 | 260,560 | 19,054 |
| el10 | 10,691,538 | 278,203 | 19,162 |
| ubuntu20.04 | 14,586,860 | 307,516 | 10,666 |
| ubuntu22.04 | 14,586,882 | 300,132 | 10,668 |
| ubuntu24.04 | 14,586,862 | 318,006 | 10,664 |
| ubuntu26.04 | 14,586,786 | 328,310 | 10,666 |
| debian11 | 14,586,876 | 297,282 | 10,662 |
| debian12 | 14,586,854 | 305,440 | 10,666 |
| debian13 | 14,586,878 | 311,938 | 10,670 |

## 范围

规范来自公开镜像中的 JEDEC 原始文档；本轮未获得 JEDEC 站点可下载的完整 PDF，
没有声称保存完整文件或原文件哈希。逐项来源及适用组合见
[来源记录](validation/ddr4-asymmetric-sources.json)。

原版全部功能仍未还原，[状态清单](recovery-status.md)保留其它 DDR4 布局、
Intel 运行时内存时序/训练、AMD PStates 写入、完整曲线/PBO/PM 表、
Intel server 域/逐核 override/TVB，以及板级 PMIC/VRM/EC/时钟协议。
SPD 容量是 EEPROM 声明的几何，不等于运行时可用容量。四台目标真机、调参稳定性和
Secure Boot 验收仍未完成；Windows/macOS 核心通过不表示已完成对应系统的硬件移植。
