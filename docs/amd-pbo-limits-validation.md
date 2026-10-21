# PBO 四项限制：整批验证

2026-10-21：生产代码验证提交 `3b374a049ada5c2e048dc8349d3b5f3b11913fc7`。
GUI / CLI 共用核心增加 Shimada PPT、VDD TDC、VDD EDC 与温度限制设置。
仅确认固件响应；当前限制回读与真机效果未验证。
见[功能与用法](amd-pbo-limits.md)、[REA 动态控制研究](amd-pbo-dynamic-research.md)、
[完整机器记录](validation/amd-pbo-limits-ci-3b374a0.json)。

| 检查 | 结果 |
|---|---|
| [共享核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39546815226) | 4/4；Ubuntu 22/24、Windows、macOS 各 28 CTest、405 场景组；新增四项限制 7 组 |
| ASan / UBSan | 五个既有离线解析目标通过；不代表新硬件设置路径有 sanitizer 或真机验收 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39546815183) | 13/13；11 个 Linux 目标各 30 CTest、57 CLI 场景组、1251 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39546815245) | 26/26；各目标及 EL7/EL8 基线各 203 项 Qt 回归；122 分析测试、34 移植测试 |
| 内核 QEMU | 12 次匹配内核启动；装卸、CPUID、能力协商、非法 CPU 和非特权拒绝通过 |
| 本机用户源码 | 50 项移植检查，37 通过、13 项因工具链/系统条件跳过 |
| 新原指令实验 | 30 个动态内存场景、281 个 EC 动态 EDC 场景；新增 3 项本机分析回归通过 |
| 真机 / Secure Boot | 未执行 |

43/43 功能验证任务均绑定同一提交，无后续运行时修补或补充截图任务。
新限制核心测试覆盖全部 6100 个有效输入编码、平台与能力门禁、单条命令和独立参数、
能力/身份变化、固件拒绝、逐访问故障与取消、超时、禁止命令与参数数量。
固件替身即使丢弃设置但返回 1，GUI / CLI 仍报告 `verified=false`。
所有寄存器设置测试使用替身，不等同于实际硬件调参成功。

Linux x86_64 覆盖 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、
Oracle Linux 7.9（EL7）、Rocky Linux 8/9/10。Windows/macOS 仅验证共享核心。
CLI 在没有显示服务、Qt、编译工具和宿主寄存器设备的干净容器完成安装、非特权诊断、卸载及重装。
包版本仍为 2.0.1，同版本更新需要使用发行版的重装方式。

归档各 Linux 目标 116 张截图，共 1276 张。本轮人工查看 Ubuntu 24.04 与 Oracle Linux 7.9 的
PPT 命令接受、温度命令接受及固件失败三种新页面，共六张；其余截图没有逐张人工检查。
截图均来自合成数据。文件哈希与检查记录包含在机器记录中。

`dist/gui-cli-amd-pbo-limits-3b374a0/` 包含 33 个 GUI/CLI/DKMS 包与 SHA256 校验表。
根目录源码包对应精确云端提交；`updated-source/` 保留八项用户扩展，并包含后续研究与验证文档。
285 个生产/构建路径逐项核对，云端工作树全部对应被测提交；用户目录仅保留已记录的八项扩展差异。
CLI 包 359,519–577,124 字节；
GUI 包 11,676,420–16,438,194 字节。

本轮实际运行 REA 查询与导出十个原函数。新增动态控制研究工具及三项回归产生于生产提交之后，
已在本机执行，不计入云端 122 项分析测试。设备、Qt、身份及定时器调用均由合成边界替代。
原动态内存函数强写 response=1 且不读完成状态，EC 动态 EDC 会截断输入到 16 位并在刷新前显示 Applied。
这些行为只作为原程序证据，尚不构成可靠的运行时协议。

新设置仅支持 AMD 1Ah/08h、PCI 1022:153a、domain 0 / BDF 0。
Fmax 仍是命令确认；rails 写入、真实逐核/CCD PM 布局、动态内存、EC 动态 EDC、
其它 AMD 平台、原生 PM 传输及未完成板级功能仍需协议或硬件证据。本批不宣称全部还原。
