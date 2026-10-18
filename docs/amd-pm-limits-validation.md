# AMD PM 原版监控区整批验证

2026-10-18：生产提交 `687a5f7008c227ec5d1bf88823e110a8d79da1b5`，GUI 与 CLI 同步增加 24 项原版 Shimada PM 限制监控值。
已有 24 项公共遥测字段保留；新数组 `original_limits` 只恢复名称、偏移和浮点存储值，
单位及物理目标未核实。支持八个精确表版本，兼容旧 schema-1 快照。
[实现与证据](amd-pm-limits.md)、[完整验证记录](validation/amd-pm-limits-ci-687a5f7.json)。

| 检查 | 结果 |
|---|---|
| 原版指令回放 | 128 例：8 版本 × 8 模拟拓扑 × 2 位模式；全部使用合成接口 |
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39095382662) | 4/4；各 24 CTest / 335 场景组，其中 AMD PM 24 组 |
| ASan / UBSan | 五个快照解析目标通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39095382589) | 13/13；11 个目标各 26 CTest / 43 CLI 场景组 / 1010 份 JSON，其中 AMD PM 报告 38 份 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39095382548) | 26/26；各目标及 EL7/EL8 基线 163 项 Qt 回归；104 分析测试、33 移植测试 |
| 匹配内核 QEMU | 12 次启动，含 EL7 3.10；装卸、能力、非法 CPU、非特权拒绝通过 |
| 本机保留扩展后的移植测试 | 49 项：37 通过、12 因 Windows 缺 Linux/C++ 工具链跳过；相应云端检查完整执行 |
| 真机调参 / Secure Boot | 未执行；合成回归与虚拟机驱动检查不能替代真机验收 |

43/43 云端任务对应同一个生产提交。首轮 `48622c2` 的 Windows 测试因局部变量重名触发
MSVC C4456，修正后整批重新构建；首轮结果未冒充最终交付证据。
本机完整分析首轮有一项既有 TVB 回放触发一秒执行时限，未改代码单独复跑两项 TVB 测试通过；
原始失败和复跑日志均保留在完整记录中。最终云端 104 项分析测试无跳过、无失败。
CLI 在无显示、无构建工具、无硬件设备的全新环境完成安装、运行、卸载和重装，未依赖 Qt/X/Wayland。

保留 924 张自动截图；人工检查 Ubuntu 24.04、Oracle Linux 7.9 的正常/异常 PM 监控页面共
4 张。具体文件、尺寸和 SHA256 见证据，不声称全部截图经过人工检查。

`dist/gui-cli-amd-pm-limits-687a5f7/` 包含 33 个包：Ubuntu 20.04/22.04/24.04/26.04、
Debian 11/12/13、Oracle Linux 7.9、Rocky Linux 8/9/10，每目标 GUI、CLI、DKMS 各一个。
EL7 验证范围为 Oracle Linux 7.9 x86_64，其他 EL7 配置仍需验收。

根目录 `octool-2.0.1-src.tar.gz` 对应精确云端提交；`updated-source/` 中的源码保留本地扩展
及最新验证记录。269 个生产/构建源码路径逐一核对，保留差异在 MANIFEST 中列出。
交付文件由 SHA256SUMS 校验。包版本仍为 2.0.1，同版本升级使用发行版的本地重装方式。

新增原版监控值不按 W/A/°C/百分比解释，也不代表 PBO 限制设置已恢复。回放假定校准完成，
未运行原固件传输、校准和每核心解析。驱动节点读取仍可能请求固件传输。
完整 PM 表、AMD 目标/单位与调参、PStates 设置、ICCmax/VR、Vmax/增强 TVB、板级控制及
真机验收仍未完成，见[剩余清单](recovery-status.md)。
