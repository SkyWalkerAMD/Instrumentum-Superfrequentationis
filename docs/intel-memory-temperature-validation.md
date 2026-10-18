# Intel DDR4 温度等级整批验证

2026-10-18：生产提交 `02a2dcd385ed11e56646747919424643d8b4f38b`。GUI 与 CLI 共用 v6 核心，增加四个 DDR4 Rank
的存储温度等级与刷新要求，B7/A700 内存读取扩展为 22 寄存器 / 83 字段。
保留 v1–v5 快照，不以类别推断实时摄氏度、当前刷新率或已核实的新鲜度。
[实现说明](intel-memory-temperature.md)、[完整验证记录](validation/intel-memory-temperature-ci-02a2dcd.json)。

| 检查 | 结果 |
|---|---|
| 依据 | Intel 743846-001 §3.2.88，逐页查看 161–162 页 |
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39097794489) | 4/4；各 24 CTest / 339 场景组，其中 Intel 内存 35 组 |
| ASan / UBSan | 五个快照解析目标通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39097781848) | 13/13；11 目标各 26 CTest / 45 CLI 场景组 / 1028 份 JSON，其中 Intel 内存 94 份 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39097781677) | 26/26；各目标及 EL7/EL8 基线 166 项 Qt 回归；104 分析测试、33 移植测试 |
| 匹配内核 QEMU | 12 次启动，含 EL7 3.10；装卸、能力、非法 CPU、非特权拒绝通过 |
| 本机用户源码移植测试 | 49 项：37 通过，12 因缺 Linux/C++ 工具链跳过；云端对应检查完整执行 |
| 真机 / Secure Boot | 未执行 |

43/43 云端任务对应同一个生产提交。CLI 在无显示、无构建工具、无硬件设备的新环境
完成安装、运行、卸载及重装，依赖中无 Qt/X/Wayland。合成测试覆盖所有类别、全部非空
Rank 掩码、DDR4/5、Gear 覆盖、保留位、版本互通、篡改重算和故障/变化/取消。

保留 946 张自动截图；人工检查 Ubuntu 24.04 与 Oracle Linux 7.9 正常/异常页面共 4 张。
其他截图未逐张人工检查。文件与哈希在完整记录中。

`dist/gui-cli-intel-memory-temperature-02a2dcd/` 包含 33 个 GUI/CLI/DKMS 包，覆盖
Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、Oracle Linux 7.9、Rocky Linux 8/9/10。
EL7 验证范围为 Oracle Linux 7.9 x86_64，其他 EL7 配置仍需验收。
根目录源码包对应精确云端提交；`updated-source/` 中的源码保留本地扩展和最新验证文档。
269 个生产/构建源码路径逐一核对，保留差异列在 MANIFEST；SHA256SUMS 覆盖全部交付文件。
包版本仍为 2.0.1，同版本更新使用发行版的本地重装方式。

DDR5 MR4 温度映射、实时新鲜度、其他 Intel 型号、PStates 写入、AMD 调参、
ICCmax/VR、Vmax/增强 TVB、板级控制与真机验收仍未完成，见[剩余清单](recovery-status.md)。

首轮未通过的任务及修正原因列在完整记录的 superseded_attempts；最终交付只使用上述提交的通过结果。
