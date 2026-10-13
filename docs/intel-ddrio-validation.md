# DDRIO 还原批次验证与交付

2026-10-13，生产提交 `3e2513417e3128f69eea6b07a82a0df89d68d098`，分支 `refactor/platform-recovery`。
GUI / CLI 新增 **11 项 DDRIO 低功耗时序与配置**；Intel 内存读取扩展为
19 个寄存器、68 个字段，v4 快照继续兼容 v1/v2/v3。BCLK 自适应电压、PL4、
能量计数、HWP 能力/历史及先前还原功能继续包含在本次完整安装包中。
[实现范围](intel-memory-ddrio.md)、[完整验证证据](validation/intel-ddrio-ci-3e25134.json)。

## 验证结果

| 检查 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38348857712) | 4/4；各 24 CTest、311 场景组，含 26 组 Intel 内存 |
| 内存与未定义行为检查 | Ubuntu 24.04 的五个快照目标 ASan/UBSan 通过 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38348861227) | 13/13；11 目标各 26 CTest、37 CLI 场景组、885 份 JSON，其中 52 份 Intel 内存报告 |
| [GUI 与驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38348856550) | 26/26；各目标及 EL7/EL8 基线均 149 项 Qt 回归；33 移植测试、99 分析测试 |
| 内核启动 | 12 套匹配内核 QEMU 启动，含 EL7 3.10；模块装卸、能力检查、非法 CPU 与非特权拒绝通过 |
| 现场写入与稳定性 | 未运行，测试均不向真实硬件调参 |

43/43 云端任务使用上述最终提交。前次尝试的 CLI 报告总数断言遗漏了新增 12 项，
修复后整批通过；修复记录保留在完整证据中。
每个 CLI 包都在新的无显示环境中完成安装、运行、卸载和重装；检查没有 Qt/X/Wayland
运行依赖。共保留 836 张自动截图，实际人工视觉检查范围另列于证据，未声称逐张查看。

## 安装包与源码

`dist/gui-cli-intel-ddrio-3e25134/` 中有 33 个包，每目标 GUI、CLI、DKMS 各一。

- EL7、EL8、EL9、EL10；其中 EL7 实测构建/安装环境为 Oracle Linux 7.9 x86_64。
- Ubuntu 20.04、22.04、24.04、26.04。
- Debian 11、12、13。

根目录的 `octool-2.0.1-src.tar.gz` 是云端精确提交源码；
`updated-source/octool-2.0.1-src.tar.gz` 额外保留用户目录的扩展、最新文档及研究记录。
`updated-source/MANIFEST.json` 记录两者哈希；`SHA256SUMS` 覆盖全部交付文件。
生产源码和构建文件共 269 项已与测试产物源码核对；本地保留差异在证据中逐一列出。
本次包版本号仍为 2.0.1；已安装同版本者需用发行版的本地包重装方式替换，
通过提交号和 SHA256 区分批次。此前交付目录保持不变。

## 边界

DDRIO 读取限定 B7/A700 裸机，允许指示的标志不表示当前功耗状态。
零配置保持原编码；部分单位未定义时显示 encoded；旧快照不会伪造新增字段。
两次读取一致不保证原子采样，尚未恢复 IOL、训练或时序写入。
Windows/macOS 目前验证共同核心，不能据此宣称完整硬件版适配。
ICCmax/VR、Vmax/增强 TVB、部分 AMD 与板级控制、目标真机及 Secure Boot 验收仍未完成，
详见[还原清单](recovery-status.md)。
