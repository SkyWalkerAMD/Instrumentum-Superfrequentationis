# BCLK、电源控制与监测还原交付

2026-10-13，本批固定生产提交 `39dc2e573568e5603611c308de9fd30def42b5ff`，GUI 和 CLI 同步。

| 新增功能 | 范围与入口 |
| --- | --- |
| BCLK 自适应电压 | B7 裸机；GUI BCLK voltage，CLI `intel-bclk-voltage-read/set`；独立开关、OC 锁、旧值检查与完整回读 |
| PL4 峰值功率 | B7 裸机；Power / performance，`intel-set --field pl4`；13 位功率字段、RAPL 单位、bit 31 锁和完整回读 |
| 能量计数 | B7 封装/PP0，8F/CF 封装/DRAM；`intel-read`；焦耳、单位和回绕范围；DRAM 需要 BIOS RAPL mode 0 |
| HWP 能力与历史 | 四个能力字节，保证性能变化与降至最低性能的两项历史；`intel-read`；只读，未知/失败不显示为零 |

[BCLK 说明](intel-bclk-voltage.md)、[PL4 说明及原版位宽/换算问题](intel-pl4.md)、[能量与 HWP 说明](intel-energy-counters.md)。
设置结果只说明配置回读一致，计数器不是瞬时功率，事件历史不是当前性能。

云端同一提交的 **43/43 个任务通过**：

- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38344186367)：4 平台各 24 CTest、308 场景组，另含五个内存/未定义行为检查目标。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38344186387)：11 目标各 26 CTest、873 份 JSON、36 场景组；无桌面新环境安装、卸载和重装通过。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38344186392)：11 目标及 EL7/EL8 两个基线各 147 项 Qt 回归，12 套匹配内核 VM；33 项移植测试和 99 项原指令研究测试。
- 本批新增四组原指令回放分别为 926、400、138、612 个场景。另有 668 个本地 STIBP/错误命名研究场景，未混入上述云端计数。
- 本地用户源码测试 49 项：37 通过，12 因 Windows 本机缺少编译工具链跳过；对应构建检查由云端完成。

目标为 Oracle Linux 7.9、Rocky Linux 8/9/10、Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13，均为 x86_64。
EL7 验证了 GLIBC 2.17 基线及 `3.10.0-1160.el7.x86_64` 模块；其它 RHEL/CentOS 7 环境没有据此自动获得验收结论。
Windows/macOS 只测试独立核心，完整硬件后端仍是 Linux。825 张截图已归档，人工检查了 Debian 11 和 EL7 的六张新功能图。

交付目录：`dist/gui-cli-intel-telemetry-39dc2e5/`。
每个发行版分别包含 GUI、CLI、DKMS 安装包，共 33 个。
根目录源码压缩包与云端生产提交一致；`updated-source/` 包含同步后的用户源码及后续研究/交付文档，保留原有扩展。
所有包仍使用版本号 2.0.1，区分批次应核对提交与 SHA256；已安装历史同版本时需要从本地包重装。
驱动仍由 DKMS 为实际内核构建 `.ko`；无需 GUI 即可使用 CLI。

[完整机器可读证据](validation/intel-telemetry-ci-39dc2e5.json)包含日志、包/模块哈希、源码三方合并和保留差异。
[功能还原清单](recovery-status.md)列出剩余范围：ICCmax/VR、Vmax/增强 TVB、部分 AMD PStates/调参和主板写入协议等仍未完成。
原版名为 perfbias 的入口实际操作 STIBP；[补充研究](intel-perfbias-research.md)固定了错误命名、跨目标复制和掩码截断问题。
真实硬件调参效果和 Secure Boot 尚未验收。
