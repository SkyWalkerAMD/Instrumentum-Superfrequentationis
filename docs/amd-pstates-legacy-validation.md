# AMD 17h / 19h PStates 批次验证

2026-10-18，生产提交 `fc330a94761654692bcad3c368a1561df73ce22b`，分支 `refactor/platform-recovery`。
GUI / CLI 同步增加 AMD Family 17h / 19h P-state 定义读取，并保留 1Ah 支持；
修正原版 DFS 09h 的频率计算，保留小数，增加范围、原始编码和逐行状态。
[实现与证据边界](amd-pstates-legacy.md)、[完整验证证据](validation/amd-pstates-legacy-ci-fc330a9.json)。
此前 HWP 包级设置、DDRIO、PL4、电压和内存等功能均包含在本批安装包中。

| 检查 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39090995407) | 4/4；各 24 CTest / 325 场景组，其中 PStates 12 组 |
| ASan / UBSan | 五个快照解析目标通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39090995314) | 13/13；11 个目标各 26 CTest / 40 CLI 场景组 / 968 份 JSON |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39090995319) | 26/26；各目标及 EL7/EL8 基线 157 项 Qt 回归；102 分析测试、33 移植测试 |
| 原指令算术分支 | 16,384 组 FID/DFS 输入；有限指令范围回放，无硬件访问 |
| 匹配内核 QEMU | 12 次启动，含 EL7 3.10；装卸、能力、非法 CPU、非特权拒绝通过 |
| 真机调参 / Secure Boot | 未执行；合成回归与虚拟机驱动检查不能替代真机验收 |

43/43 云端任务对应同一个生产提交。源码与 GUI/CLI/驱动使用相同版本；CLI 在无显示、
无构建工具、无硬件设备的全新环境中完成安装、运行、卸载和重装，未依赖 Qt/X/Wayland。
保留 880 张自动截图；人工检查的具体图片和 SHA256 见证据，不声称全部截图经过人工检查。

`dist/gui-cli-amd-pstates-legacy-fc330a9/` 包含 33 个包：Ubuntu 20.04/22.04/24.04/26.04、
Debian 11/12/13、Oracle Linux 7.9、Rocky Linux 8/9/10，每目标 GUI、CLI、DKMS 各一个。
EL7 实测范围是 Oracle Linux 7.9 x86_64；其他 EL7 配置仍需验收。

根目录 `octool-2.0.1-src.tar.gz` 对应精确云端提交；`updated-source/` 中的源码保留本地扩展和
最新验证记录。269 个生产/构建源码路径已逐一核对，保留差异在 MANIFEST 中列出。
全部交付文件由 SHA256SUMS 校验。包版本仍为 2.0.1，同版本升级请使用发行版的本地重装方式。

这批查询显示配置频率，不代表实时频率；17h/19h VID 只显示低字节片段，bit 32 单独保留。
未确认的 DFS 编码不生成频率。读取期间 CPU 选择发生任何变化，GUI 会丢弃旧请求结果。
PStates 写入、型号专用电压/电流换算、ICCmax/VR、增强 TVB、其它 AMD/主板调参和真机验收
仍未完成，见[剩余清单](recovery-status.md)。
