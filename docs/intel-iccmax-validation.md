# Intel IA ICCmax 整批验证

2026-10-18：生产提交 `dbe92061c5e1635f58106cae94b351927a0c1c88`，GUI / CLI 共用 IA 电流字段读取与设置核心。
限定 Alder Lake 97h/9Ah 的九个明确型号/stepping 组合；完整核验 SVID 拓扑、旧值、
CPU/锁上下文和回读。bit 31 模式语义保持未确认，置位时只读。
[实现说明](intel-iccmax.md)、[完整证据](validation/intel-iccmax-ci-dbe9206.json)。

| 检查 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39104710608) | 4/4；各 25 CTest / 357 场景组，含 12 组 ICCmax 回归 |
| ASan / UBSan | 原有五个离线快照解析目标通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39104713643) | 13/13；11 目标各 27 CTest / 48 CLI 场景组 / 1098 JSON，新增 43 份 ICCmax 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39104678684) | 26/26；各目标及 EL7/EL8 基线 175 项 Qt 回归，104 分析测试 / 33 移植测试 |
| 内核 QEMU | 12 次匹配内核启动；装卸、CPUID、能力、非法 CPU、非特权拒绝通过 |
| 本机源码检查 | 用户扩展版 49 项：37 通过、12 缺 Linux/C++ 工具链而跳过；云端对应检查完整执行 |
| 真机 / Secure Boot | 未执行 |

43/43 最终任务绑定同一提交。先前两次尝试分别修正合成夹具的邮箱地址和 CLI 报告数量；
随后人工截图检查发现只读页在 Qt 完成布局前被截取，补上布局就绪断言再完整验证。
记录保留各次运行编号与证据；此前产物不作为本批最终通过结果或交付产物。
Windows/macOS 仅验证独立核心；Linux 目标为 Ubuntu 20.04/22.04/24.04/26.04、
Debian 11/12/13、Oracle Linux 7.9、Rocky Linux 8/9/10。EL7 为 Oracle Linux 7.9 x86_64。

新增核心覆盖所有 2048 编码、16 个 IA 地址、支持 stepping 与虚拟化/能力门控、
逐位旧值/拓扑/锁变化、所有读写故障位置、255 种固件状态、busy、取消/期限及完整读回。
GUI 覆盖复制、输入步进、确认取消、无变化请求、只读状态、失败清空、目标切换及关闭竞态。
这些场景使用合成设备，没有执行硬件调参。

保留 1034 张截图，人工检查 Ubuntu 24.04 与 Oracle Linux 7.9 的成功设置、模式位只读和
读取错误页面各三张，共六张；其余 1028 张未逐一人工检查。所有显示值均为合成输入。

`dist/gui-cli-intel-iccmax-dbe9206/` 包含 33 个 GUI/CLI/DKMS 包、精确云端源码、保留本地扩展
的 `updated-source/` 源码、MANIFEST 和 SHA256SUMS。275 个生产/构建源码路径逐一核对，
保留八个既有本地扩展。包版本仍为 2.0.1；更新时使用发行版的本地重装方式。

IA 配置编码不等于测得电流或有效限流值。unlimited 模式解释/切换、Raptor Lake 与服务器
ICCmax、其它电压轨、Vmax/增强 TVB、AMD 高层调参及主板专用控制仍在[剩余清单](recovery-status.md)中。
