# Intel 节能控制：验证交付

生产提交 `61a04948c17ab1ee3845dcba6ef5f3914cc39b94`，开发分支 `refactor/platform-recovery`。
[完整证据](validation/intel-energy-ci-61a0494.json)绑定同一提交的任务、日志、图像、安装包和源码哈希。

GUI `Intel Controls → Energy optimization`、CLI `intel-energy-read/set` 同步恢复
[Energy Efficient Turbo 与 Race to Halt](intel-energy.md)。限定 B7 裸机，显式指定逻辑 CPU。
两项分别使用 MSR 1FC 禁用位 19、20，对外 1 表示启用；每次只改变一位，完整比较旧值和读回。
相同设置不写入，失败不重试或回滚。未广播到其它 CPU，也未推断硬件节能效果。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38323413941) | 4/4；各 19 CTest、268 场景组 |
| ASan / UBSan | 既有离线解析和快照的 5 项检查通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38323414012) | 12/12；十目标各 21 CTest、27 组命令场景、657 份 JSON |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38323413900) | 23/23；十目标及 EL8 基线各 115 Qt 测试，11 套目标内核 VM |
| 云端原版分析回归 | 83 项通过；三个节能辅助函数的 434 场景与固定证据一致 |
| 本机原版分析回归 | 83 项：80 通过、3 因缺少 Linux GCC/binutils 跳过 |
| 本机用户目录兼容检查 | 48 项：36 通过、12 因工具链缺失跳过；文档和确定性源码归档检查通过 |

新核心增加 12 场景组，覆盖两项开关全部 64 位旧值冲突、回读逐位损坏、不同故障位置、
型号/身份/能力变化、取消、超时、固件忽略和不变路径。
GUI 新增 6 项测试，包含双向切换、复制、确认取消、确认期间 CPU 变化、异步目标变化和关闭窗口。
CLI 新增 72 份报告：16 项参数拒绝、56 份功能和错误报告；合法全零值与读取失败的 null 独立验证。

十个 CLI 目标在无桌面、无 Qt、无编译工具环境中安装、普通用户启动、卸载并重装。
保留 680 张图像；人工检查范围和哈希见 `visual_review`，未声称逐张人工检查全部图像。
同步本轮 32 个文件；最终文档补充前 25 个与生产源码一致。
六份历史文档保留各自记录；用户工作流只增加四行分析步骤/产物，九项独立 NVL 检查保留。
本轮 19 个运行时、测试、分析及构建文件在两个目录中均匹配生产提交。

## 交付

`dist/gui-cli-intel-energy-61a0494/`：十个 GUI、十个 CLI、十个可选 DKMS 包，
源码、源码哈希侧车和 SHA256SUMS，共 33 个文件。版号仍为 2.0.1，以提交目录及哈希区分。
源码 4,718,045 字节，SHA256 `32e24b9ab5d486087a46ee656335ee8d3c3a17b9cb41c9df531bbf17241df762`。
安装包和源码固定于生产提交；最终验证文档由后续文档提交补充。

| 目标 | GUI 字节 | CLI 字节 | DKMS 字节 |
|---|---:|---:|---:|
| el8 | 11,390,592 | 309,672 | 19,072 |
| el9 | 10,980,978 | 305,069 | 19,060 |
| el10 | 10,984,862 | 335,931 | 19,164 |
| ubuntu20.04 | 15,072,258 | 366,314 | 10,668 |
| ubuntu22.04 | 15,072,308 | 359,646 | 10,668 |
| ubuntu24.04 | 15,072,312 | 399,322 | 10,668 |
| ubuntu26.04 | 15,072,250 | 410,168 | 10,666 |
| debian11 | 15,072,246 | 358,096 | 10,670 |
| debian12 | 15,072,248 | 376,880 | 10,664 |
| debian13 | 15,072,266 | 390,224 | 10,668 |

## 边界

配置回读不代表真实频率、功耗、性能或稳定性测量，也不认证其它核心的设置。
增强 TVB、Vmax、逐物理核心表、其它 Intel 型号、AMD P-state 写入、完整 MP1/PBO、
板级调节等仍有缺口。目标真机及 Secure Boot 验收尚未完成。
Windows/macOS 结果仅表示独立核心通过，不表示完整硬件或 GUI 移植。
