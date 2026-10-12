# Intel TVB 温度与降倍率分档查询：验证交付

生产提交 `aca1652b776e573950e33f88c10dcd5052934887`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/intel-tvb-table-ci-aca1652.json)绑定同一源码的云端任务、日志、截图、安装包和 SHA256。

GUI `Intel Controls → TVB thresholds` 与 CLI `intel-tvb-table` 同时增加
[分档查询](intel-tvb-table.md)，限定 Intel 06_B7 裸机。支持单项或八项候选扫描，
显示两组温度、降倍率、原始值和逐项固件错误。它不会发送分档设置指令，
不会将候选编号当作物理核心号，也不会把成功应答当作 SKU 能力证明。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38225310065) | 4/4；各 17 CTest、244 场景组，含 8 组新增 TVB 表查询 |
| ASan / UBSan | 既有 SPD、PM、UMC、Intel 内存和时钟快照共 5 项通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38225315230) | 12/12；十目标各 19 CTest、25 组命令场景、520 份 JSON，含 18 份新表查询报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38225308264) | 23/23；十目标及 EL8 基线各 102 Qt 测试、11 套目标内核 VM |
| 分析回归 | 79 项通过；原版四函数 88 场景云端重放与本地证据一致 |
| 用户原目录本机 | 48 项：36 通过、12 因本机缺工具链跳过；文档、确定性源码归档和归档内容检查通过 |

新增回归覆盖所有四字节边界、每个查询编号、型号/虚拟机/MSR 门禁、全部 255 种非零固件状态、
每个传输故障位置、忙超时、取消、截止时间及首次完成采样。GUI 验证单项/全部查询、部分失败、
复制、无效 CPU、选择变更时丢弃异步结果和关闭窗口。查询期间没有设置命令。
CLI 在十套无桌面、无 Qt、无构建工具的独立环境完成安装、普通用户启动、卸载和重装。

归档 640 张截图；人工检查的页面和文件哈希见证据的 `visual_review`。
31 个本轮文件同步到用户原目录，24 个换行规范化后与生产提交相同；
六份历史文档应用本轮增量，独立工作流只增加 4 行，保留全部 9 项额外 NVL 检查。
19 个本轮运行时、测试、分析和构建文件完整匹配生产源码；驱动 / HAL / ABI 沿用此前实现。

## 交付文件

`dist/gui-cli-intel-tvb-table-aca1652/` 包含 10 GUI、10 CLI、10 可选 DKMS 包、源码、
源码 SHA256 侧车和 `SHA256SUMS`，共 33 个文件。版号仍为 2.0.1，以提交目录及哈希区分。
源码 4,577,868 字节，SHA256 `6733d626cca8b4bb38a097a6c9ffc2adf1d2764303dfa05dec6b6f17e7ed5a72`。
包和源码固定于生产提交；最终验证记录由后续文档提交补充。

| 目标 | GUI 字节 | CLI 字节 | DKMS 字节 |
|---|---:|---:|---:|
| el8 | 11,280,928 | 303,264 | 19,068 |
| el9 | 10,901,891 | 299,048 | 19,061 |
| el10 | 10,897,563 | 327,399 | 19,157 |
| ubuntu20.04 | 14,910,446 | 357,640 | 10,666 |
| ubuntu22.04 | 14,910,454 | 351,192 | 10,666 |
| ubuntu24.04 | 14,910,642 | 389,824 | 10,670 |
| ubuntu26.04 | 14,910,642 | 399,382 | 10,670 |
| debian11 | 14,910,472 | 349,540 | 10,666 |
| debian12 | 14,910,648 | 367,994 | 10,670 |
| debian13 | 14,910,644 | 379,680 | 10,670 |

## 未完成范围

本轮恢复的是分档读取；分档写入、逐物理核心表、一键 Boost、Vmax stress、Ring DownBin 和其它型号尚未恢复。
数据为逐项配置采样，不能据此证明 TVB 正在生效或推出实测电压、频率、温度。
AMD P-state 写入、完整 MP1/PBO 与板级调节等缺口仍在，目标真机和 Secure Boot 尚未验收。
Windows/macOS 只验证独立核心，不代表完整 GUI 或硬件适配。
