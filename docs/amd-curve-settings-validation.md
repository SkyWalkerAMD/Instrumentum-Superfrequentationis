# AMD 曲线设置整批验证

2026-10-21：生产提交 `8c5424b1cad22afbbd319fb90dba4b5ae7170027`。
GUI 和 CLI 同步加入 Shimada 1Ah/08h 单核及同 CCD 选核 CO count 设置，
先比较旧值，再逐核写入和回读。见[实现说明](amd-curve-settings.md)和
[完整证据](validation/amd-curve-settings-ci-8c5424b.json)。

| 检查 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39514671732) | 4/4；Ubuntu 22/24、Windows、macOS 各 25 CTest、381 场景组；曲线 15 组 |
| ASan / UBSan | 五个既有离线解析目标通过；不代表真实硬件曲线写入验收 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39514671815) | 13/13；11 个 Linux 目标各 27 CTest、52 CLI 场景组、1177 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39514671813) | 26/26；各目标及 EL7/EL8 基线 190 项 Qt 回归；108 分析测试、33 移植测试 |
| 内核 QEMU | 12 次匹配内核启动；装卸、CPUID、能力协商、非法 CPU、非特权拒绝通过 |
| 本机用户源码移植测试 | 49 项：37 通过，12 项因缺 Linux/C++ 工具链跳过 |
| 新增原指令实验 | 80 组 setter、12 个故障位置、9 组 A8 返回；另 3 项新增分析回归本机通过 |
| 真机 / Secure Boot | 未执行 |

43/43 功能验证任务绑定同一生产提交。三个新增原指令分析测试在生产提交之后加入，
单独本机执行，不混入该提交的 108 项云端分析计数。
核心测试覆盖 count 与目标编码、无效输入零访问、旧值冲突、仅部分核心完成、
固件拒绝、回读不符、每个访问位置失败、各边界取消、期限和身份改变。
GUI 新增五项回归，包含取消确认、确认期间改目标和异步旧结果失效。

CLI 在干净容器中安装、使用、卸载和重装；没有显示服务、Qt 或宿主寄存器设备。
EL7 为 Oracle Linux 7.9 x86_64；其余为 Ubuntu 20.04/22.04/24.04/26.04、
Debian 11/12/13、Rocky Linux 8/9/10。Windows/macOS 只验证共享核心。
包版本仍为 2.0.1；同版本本地更新应使用发行版的重装方式。

保留 1177 张截图，人工查看 Ubuntu 24.04 与 Oracle Linux 7.9 的选核读取、
成功回读、部分失败页面各三张，共六张；其余截图未逐一人工检查。画面全部是合成数据。

`dist/gui-cli-amd-curve-settings-8c5424b/` 包含 33 个 GUI/CLI/DKMS 包，
根目录源码包对应精确云端提交，`updated-source/` 包含同步后的用户源码、
八项既有本地扩展和后续研究文档。276 个生产/构建路径逐一核对，MANIFEST 与 SHA256SUMS
区分两种源码包。CLI 包 344,704–561,976 字节；
GUI 包 11,492,623–16,109,612 字节。

本轮实际使用 REA 6.7.0 / Ghidra 12.1.4 完成 12 个函数分析，见
[REA 任务](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39435187923)。
原始指令回放确认了旧 setter 的保留位、重复命令和失败处理问题；新核心按已核对协议收紧。
回读成功只说明固件数值匹配；真实物理核心、端口/初始化兼容性、电压效果与稳定性仍待真机验收。
逐核/CCD PM 布局、其它平台曲线/Curve Shaper、完整 PBO、原生 PM 与板级功能仍未全部还原。
