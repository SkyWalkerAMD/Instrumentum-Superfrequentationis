# Intel Ring DownBin：验证交付

生产提交 `c6e95991cdcd57fef61c58624e38a0ccf3c021a3`，研究分支 `refactor/platform-recovery`。
[完整证据](validation/intel-ring-ci-c6e9599.json)绑定相同提交的云端任务、日志、图像、安装包和源码哈希。

GUI `Intel Controls → TVB & Ring DownBin` 和 CLI `intel-tvb-read/set` 同步恢复
[Ring DownBin 读取与单项设置](intel-ring-downbin.md)。限定 B7 裸机，设置要求固件 OC lock 未锁定。
enabled=1 清除禁用位 bit 1，enabled=0 设置该位；保留同一配置字的另外 31 位。
写前核对旧值与身份/锁上下文，写后验证完整回读；无变化不写设置，失败不自动重试或回滚。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38258982087) | 4/4；各 18 CTest、256 场景组 |
| ASan / UBSan | 既有离线解析和快照的 5 项检查通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38258988264) | 12/12；十目标各 20 CTest、26 组命令场景、585 份 JSON |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38258946852) | 23/23；十目标及 EL8 基线各 109 Qt 测试，11 套目标内核 VM |
| 分析回归 | 81 项通过；原版 Ring / Vmax setter 的 156 场景与固定证据一致 |
| 本机用户目录 | 48 项：36 通过、12 因工具链缺失跳过；文档和确定性源码归档检查通过 |

Ring 字段经过全部 255 种非零固件状态、每个传输故障位置、完整 32 位旧值与回读变化、
锁/型号检查、超时、取消、截止时间和无变化路径。GUI 测试覆盖三项配置显示、复制、
Ring 双向编辑、确认取消、确认期间 CPU 改变和写入失败后的旧值清空。
CLI 增加 21 份报告（6 组非法参数与 15 份功能报告），其中 Vmax 两个名称仍在硬件打开前被拒绝。

十个 CLI 目标在独立无桌面、无 Qt、无编译工具的环境安装、普通用户启动、卸载并重装。
归档 650 张图像；人工检查范围与图像哈希见 `visual_review`，未声称人工检查全部图像。
同步 25 个本轮文件；最终文档补充前 18 个与生产源码一致。
六份历史文档仅增加本轮说明；用户工作流只增加四行分析步骤/产物记录，保留九项独立 NVL 检查。
本轮 12 个运行时、测试和分析文件在两个目录中均匹配生产提交。

## 交付文件

`dist/gui-cli-intel-ring-c6e9599/` 包含十个 GUI、十个 CLI、十个可选 DKMS 包，
以及源码、源码哈希侧车和 `SHA256SUMS`，共 33 个文件。版号仍为 2.0.1，以目录提交号与哈希区分。
源码 4,666,313 字节，SHA256 `c2368a20f83089c7c73b9a25ee04475c590e29ba2b7954f01491e41c2a99dc0d`。
包和源码固定于生产提交；这份最终验证文档在随后文档提交中补充。

| 目标 | GUI 字节 | CLI 字节 | DKMS 字节 |
|---|---:|---:|---:|
| el8 | 11,347,200 | 307,176 | 19,068 |
| el9 | 10,948,960 | 303,126 | 19,055 |
| el10 | 10,947,619 | 333,528 | 19,156 |
| ubuntu20.04 | 15,004,370 | 362,844 | 10,666 |
| ubuntu22.04 | 15,004,372 | 356,560 | 10,668 |
| ubuntu24.04 | 15,004,366 | 396,596 | 10,670 |
| ubuntu26.04 | 15,004,360 | 406,406 | 10,668 |
| debian11 | 15,004,426 | 355,052 | 10,666 |
| debian12 | 15,004,362 | 373,986 | 10,666 |
| debian13 | 15,004,366 | 387,220 | 10,666 |

## 验证边界

配置回读不测量实际 Ring 频率、电压或性能，不能代替 B7 真机调节与稳定性验收。
Vmax 原版位操作已解析；较早平台的官方定义尚不足以确认 B7 运行语义，本轮仍保留该位。
增强 TVB、逐物理核心表、其它 Intel 型号、AMD P-state 写入、完整 MP1/PBO、板级调节等仍待恢复。
Secure Boot 验收未完成。Windows/macOS 结果仅表示独立核心通过，不表示完整硬件或 GUI 移植。
