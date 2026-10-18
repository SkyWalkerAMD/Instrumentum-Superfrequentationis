# Intel ODT 与写数据延迟整批验证

2026-10-18：生产提交 `cf3e50a36a24e8dd8684d8b977003e2ce528b21f`，GUI 与 CLI 同步增加 8 个 ODT 引脚掩码和
3 个写数据延迟配置，限定 Raptor Lake-S B7/A700。读取快照 v5 为 21 个寄存器、79 个字段，
继续兼容 v1–v4；[实现和来源](intel-memory-odt.md)、[完整证据](validation/intel-memory-odt-ci-cf3e50a.json)。
此前 AMD 17h/19h/1Ah PStates、HWP 包级设置、PL4、DDRIO 等功能均包含在安装包中。

| 检查 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39092938362) | 4/4；各 24 CTest / 330 场景组，其中 Intel 内存 31 组 |
| ASan / UBSan | 五个快照解析目标通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39092938383) | 13/13；11 个目标各 26 CTest / 42 CLI 场景组 / 992 份 JSON，其中 Intel 内存报告 76 份 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39092938425) | 26/26；各目标及 EL7/EL8 基线 160 项 Qt 回归；102 分析测试、33 移植测试 |
| 匹配内核 QEMU | 12 次启动，含 EL7 3.10；装卸、能力、非法 CPU、非特权拒绝通过 |
| 本机保留扩展后的测试 | 49 项：37 通过、12 因 Windows 缺 Linux/C++ 工具链跳过；相应云端检查完整执行 |
| 真机调参 / Secure Boot | 未执行；合成回归与虚拟机驱动检查不能替代真机验收 |

43/43 云端任务对应同一个生产提交。CLI 在无显示、无构建工具、无硬件设备的全新环境
完成安装、运行、卸载和重装，未依赖 Qt/X/Wayland。
保留 902 张自动截图；人工检查 Ubuntu 24.04、Oracle Linux 7.9 的 DDR4/DDR5 ODT 页面共
4 张，具体文件和 SHA256 见证据；不声称全部截图经过人工检查。

`dist/gui-cli-intel-memory-odt-cf3e50a/` 包含 33 个包：Ubuntu 20.04/22.04/24.04/26.04、
Debian 11/12/13、Oracle Linux 7.9、Rocky Linux 8/9/10，每目标 GUI、CLI、DKMS 各一个。
EL7 验证范围为 Oracle Linux 7.9 x86_64，其他 EL7 配置仍需验收。

根目录 `octool-2.0.1-src.tar.gz` 对应精确云端提交；`updated-source/` 中的源码保留本地扩展
及最新验证记录。269 个生产/构建源码路径逐一核对，保留差异在 MANIFEST 中列出。
交付文件由 SHA256SUMS 校验。包版本仍为 2.0.1，同版本升级使用发行版的本地重装方式。

新增项均为只读配置。ODT 掩码不证明矩阵已启用、不代表电阻值；写延迟按 DCLK 与标志
分别展示，不推算有效 tCWL、IOL 或实时 ns。没有添加时序写入、训练和未知平台寄存器访问。
PStates 设置、ICCmax/VR、Vmax/增强 TVB、其它 AMD/主板调参及真机验收仍未完成，
见[剩余清单](recovery-status.md)。
