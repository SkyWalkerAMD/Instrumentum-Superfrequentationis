# Intel TVB 两项配置开关：验证交付

生产提交 `743c40fc2fd114e60f26df6fd7dbff3b4b609acf`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/intel-tvb-ci-743c40f.json)绑定同一源码的云端任务、日志、截图、安装包和 SHA256。

GUI `Intel Controls → TVB configuration` 与 CLI `intel-tvb-read/set` 同步恢复
[TVB 高温倍频裁剪与电压优化](intel-tvb.md)。限定 Intel 06_B7 裸机，需成功固件应答；
设置需 OC lock 未设置。统一以 1 表示启用、0 表示禁用，转换原版反向 disable bit。
设置前比较旧值，保留其余 31 位，设置后核验完整配置与身份/锁；相同值不写设置。
错误、取消、超时、CPU 变化均不展示无效旧值；不自动重试或回滚设置。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38219382818) | 4/4；各 16 CTest、236 场景组，含 10 组 TVB |
| ASan / UBSan | 既有 SPD、PM、UMC、Intel 内存和 Intel 时钟快照共 5 项通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38219387307) | 12/12；十目标各 18 CTest、24 组命令场景、493 份 JSON，含 20 份 TVB 硬件模拟报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38219380980) | 23/23；十目标及 EL8 基线各 98 Qt 测试、11 套目标内核 VM |
| 分析回归 | 76 项通过；原版 TVB 三函数 47 场景云端重放与本地证据一致 |
| 用户原目录本机 | 48 项：36 通过、12 因本机缺工具链跳过；文档、确定性源码归档和归档内容检查通过 |

新增核心回归覆盖两个开关的正反转换与位保留、CPU/虚拟机/锁门禁、旧值变化、每一个传输失败位置、
固件失败、busy 有界超时、回读每一位变化、取消及截止时间。GUI 同时验证更换目标、关闭窗口、
取消确认、相同值、失败后清空状态。十套 CLI 在无桌面、无 Qt、无构建工具的独立环境完成
安装、普通用户启动、卸载和重装。

归档 610 张截图；实际人工检查页面及哈希见证据中的 `visual_review`。
33 个本轮文件同步原目录，其中 26 个换行规范化后与生产提交相同；六份历史文档只应用本轮增量。
原目录独立工作流仅增加 TVB 的 4 行，没有删除原有内容，保留全部 9 项额外 NVL 分析检查。
20 个本轮运行时、测试和构建文件完整匹配云端生产源码。驱动 / HAL / ABI 沿用此前实现。

## 交付文件

`dist/gui-cli-intel-tvb-743c40f/` 包含 10 GUI、10 CLI、10 可选 DKMS 包，源码、
源码 SHA256 侧车和 `SHA256SUMS`，共 33 个文件。版号仍为 2.0.1，以提交目录及哈希区分。
源码 4,522,359 字节，SHA256 `a64a6fab0418696e3e12a0bf91d71d9dbda1fd36b56f6ec7d69d8ca86b42c08e`。
包和源码固定于生产提交；最终验证记录由其后的文档提交补充。

| 目标 | GUI 字节 | CLI 字节 | DKMS 字节 |
|---|---:|---:|---:|
| el8 | 11,234,996 | 300,264 | 19,068 |
| el9 | 10,850,192 | 296,611 | 19,058 |
| el10 | 10,850,323 | 324,983 | 19,163 |
| ubuntu20.04 | 14,847,620 | 354,020 | 10,666 |
| ubuntu22.04 | 14,847,596 | 348,738 | 10,664 |
| ubuntu24.04 | 14,847,588 | 387,046 | 10,666 |
| ubuntu26.04 | 14,847,610 | 396,804 | 10,666 |
| debian11 | 14,847,630 | 347,218 | 10,664 |
| debian12 | 14,847,622 | 364,974 | 10,666 |
| debian13 | 14,847,606 | 376,908 | 10,672 |

## 未完成范围

配置回读成功不等于该 SKU 正在执行 TVB，也不证明频率、电压、温度或性能达到预期。
温度/倍频分档表、每物理核心覆盖、Vmax stress、Ring DownBin、其它客户端和服务器协议仍未由本轮恢复。
AMD P-state 写入、MP1/PBO 单位与目标映射、板级调节等原有缺口仍在。
目标真机调节和 Secure Boot 验收尚未进行；Windows/macOS 仅验证可移植核心，不是完整硬件适配。
