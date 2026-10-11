# XMP 元数据与堆叠内存组织：GUI / CLI 验证

生产提交 `6f544e692ddfa5b19f1523d09cc8ad9e562b3f7c`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/spd-profile-geometry-ci-6f544e6.json)绑定云端运行、日志、安装包、源码及 SHA256。
[实现说明](spd-profile-geometry.md)与[固定来源](validation/spd-profile-geometry-sources.json)说明解码条件。

GUI 和 CLI 同步新增 XMP 2.0 的 CL7..24 位图与每个配置的推荐 DIMM 数；
补充 DDR4 12/24 Gbit 密度、两代对称堆叠容量和 DDR5 3DS 不同逻辑 rank 刷新要求。
DDR5 子通道计数修正为 1/2/4/8，并分别检查基础区与模块公共区修订版。
原始编码、未知状态和 CRC 保留；存储推荐数不当作实际安装数量。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38125934367) | 4/4；各 12 CTest、145 场景组，含 40 组 SPD 场景 |
| ASan / UBSan | Ubuntu 24.04 的 SPD 与 UMC 检查通过 |
| [独立 CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38125934384) | 12/12；十目标各 14 CTest、15 组命令场景、327 份 JSON |
| [Linux GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38125934404) | 23/23；十目标及 EL8 基线各 66 Qt 测试，11 套内核 VM |
| 既有原指令/分析回归 | 74 项通过；不表示执行了原版 SPD 函数 |
| 原目录本机检查 | 48 项：36 通过、12 因工具链/环境跳过；归档确定性、完整回读与链接检查通过 |

新增八组核心场景覆盖：两组 XMP CAS 各字节全部 256 编码；配置字节全部 256 编码；
DDR4 密度/封装；DDR5 密度/封装；全部子通道/总线编码与器件宽度组合；3DS 刷新各字节；
两个组织修订版各 256 值；截断与基础 CRC 失败。独立算例区分多负载和 3DS，避免重复乘 die 数。
CLI 新增 14 份离线报告，共 68 份 SPD 报告，检查全过程不创建硬件后端。
GUI 新增两项测试，检查字段数值、保留位、禁用档案、版本变化、短文件和错误后清除旧结果。
保存 410 张截图，实际人工查看范围见证据清单；合成 SPD 不冒充目标机器采集。

EL10 首次任务在安装依赖时因 EPEL 10.2 缺失索引返回 404，尚未编译源码。
保留首次失败日志与任务记录；同一生产提交重跑该目标及随之失败的汇总门禁后通过，其余已通过任务未重跑。

CLI 在十个无显示、无 Qt、无构建工具的运行镜像中验证安装、普通用户启动、卸载和重装。
JSON 版本仍为 1，新增 `cas.encoding`、可空 `recommended_dimms_per_channel`；
基础 `fields` 的值继续为字符串。XMP 2.0 没有扩展 CRC，保留位不解释成基础区的范围选择。

## 交付

`dist/gui-cli-spd-profile-geometry-6f544e6/`：10 GUI + 10 CLI + 10 可选 DKMS 包，
另附源码、源码 SHA256 侧车和 SHA256SUMS，共 33 文件。研究版号仍为 2.0.1，以提交目录和校验值区分。
本轮 22 个文件已同步原目录：21 个按换行及既有 UMC 链接规范化后与云端/源码一致；
`platform-controls.md` 的 SPD 行一致，其独立 UMC 内容保留。驱动/HAL/ABI 未改。
安装包和源码固定于生产提交；本验证报告、索引和 PStates 后续来源核对单独记录在后续提交。

源码 4,150,236 字节；SHA256 `30914d6839851031b83baafe7ceb032326581986c284c7cc365ccbe15dc7c309`。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,897,740 | 223,620 | 19,072 |
| el9 | 10,567,116 | 225,107 | 19,059 |
| el10 | 10,567,368 | 236,673 | 19,162 |
| ubuntu20.04 | 14,402,022 | 265,186 | 10,670 |
| ubuntu22.04 | 14,401,942 | 257,230 | 10,668 |
| ubuntu24.04 | 14,401,942 | 266,444 | 10,668 |
| ubuntu26.04 | 14,402,028 | 273,868 | 10,668 |
| debian11 | 14,402,014 | 252,702 | 10,670 |
| debian12 | 14,402,040 | 254,920 | 10,668 |
| debian13 | 14,401,992 | 260,210 | 10,668 |

原版功能尚未全部恢复。非对称/混合 rank 容量、XMP 用户档案/XMP 3.0 推荐 DIMM 数、EXPO 增强时序、
实时训练值及 SPD/板级写入仍未完成。AMD/Intel 其它缺口见[状态表](recovery-status.md)，
最新 PPR 核对见[AMD 写入研究](amd-topology-recovery.md)。真机调参、稳定性、Secure Boot 登记与
跨版本升级没有验收；Windows/macOS 核心通过也不表示其完整硬件后端已完成。
