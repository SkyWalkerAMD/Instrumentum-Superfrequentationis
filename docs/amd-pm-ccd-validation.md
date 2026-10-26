# PM 固件 CCD 映射整批验证

GUI / CLI / 安装包验证提交为 `168a6186598c31630385cb344c57d55b1d519d76`。
核心验证提交为 `43bfbb9b655f89bdce19a03ab8c3e78d7214eb30`，REA 研究提交为 `b043e8e1c4dac396ce9e29e6a0dd1ed45f8bcc8c`。
CLI 后续修正了报告计数；核心后续只增加测试时限、耗时诊断和 CI 并行度。
运行时代码逐文件一致，最终源码保留已验证的核心测试修正。
本轮交付位于 `dist/gui-cli-amd-pm-ccd-168a618/`，软件版本仍为 2.0.1。

| 验证 | 结果 |
|---|---|
| [核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40329328883) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14，各 34 CTest、459 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40327094587) | 13/13；11 Linux 目标各 36 CTest、73 CLI 场景组、1527 JSON 报告 |
| [GUI / 内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40327094952) | 26/26；各目标和 EL7/EL8 baseline 各 232 Qt 测试；142 分析、34 移植测试 |
| [REA / 固件](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40326286620) | 1/1；738 个 CCD 映射案例、11472 个合成编码及既有 PM/rails/Fmax/功耗调整回放通过 |
| 安装与显示 | 11 目标 GUI 启动；CLI 在无显示、无 Qt、无编译器环境完成安装/卸载/重装及非 root diagnose |
| 内核集成 | 12 次匹配内核的 QEMU 启动；未执行真机寄存器设置和 Secure Boot 验收 |
| 用户源码扩展 | 50 项 port 测试：37 通过，13 因 Windows 环境跳过 |

最终通过 **44/44** 云端任务；首轮 CLI/移植任务及 Windows/macOS 测试预算不足的核心任务未计入。
新回归覆盖八个表宽度、稀疏编号 0/15、活动序号与存在序号分离、前后掩码变化、
版本/计数不匹配、每个目标的通信失败和取消。失败不发布部分映射或 PM 表。
GUI 覆盖按固件 CCD 标注逐核/CCD 存储槽位、输入变化清除旧结果、捕获失败和导入文件不信任 live 声明。
五项既有离线解析 ASan/UBSan 检查通过。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 为 Oracle Linux 7.9；Windows/macOS 仅验证可移植核心。
每个 Linux 目标保存 135 张截图，共 1485 张；人工检查 EL7 与 Ubuntu 24.04 的新映射视图。
295 个运行/构建文件和 12 个研究文件均核对成功任务源码。
用户目录保留八项原有扩展和 UMC 文档别名。交付包含 33 个 GUI/CLI/DKMS 包、
精确云端源码包及保留用户扩展的 `updated-source/`，由 `SHA256SUMS` 校验。

[协议与用法](amd-pm-ccd-mapping.md)、[完整应用验证](validation/amd-pm-ccd-ci-168a618.json)、
[REA 研究证据](validation/amd-pm-ccd-research.json)。
当前完成的是固定 Shimada 固件的 CCD 存储槽位映射；物理单位、活动核心、OS CPU/APIC 映射和第二 rails 回读仍未完成。
用户指定的 **9950X3D 属于 Granite Ridge，尚不能使用这条 Shimada 路径**，需独立适配。
未进行 9950X3D 或其它处理器的真机硬件验收。
