# Fmax 回读整批验证

GUI、CLI 和核心在 `fca07259077e678d3660f4161081cfdf7aebda5e` 通过验证；本轮源码与包位于
`dist/gui-cli-amd-fmax-readback-fca0725/`。版本号仍为 2.0.1。

| 验证 | 结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40310910150) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14，各 34 CTest、456 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40310910103) | 13/13；11 Linux 目标各 36 CTest、72 CLI 场景组、1521 JSON 报告 |
| [GUI / 内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40310909932) | 26/26；各目标及 EL7/EL8 baseline 各 231 Qt 测试；142 分析、34 移植测试 |
| [REA / Xtensa 研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/40314248220) | 1/1；11472 个合成指令编码；510 个 Fmax 设置案例及两种更新地址语义比较、420 次读回；原有 PM 复制、rails、A7/A8 回放通过 |
| 安装与显示 | 11 目标 GUI 启动；CLI 无显示、无 Qt、无编译器的安装、卸载、重装和非 root diagnose |
| 内核集成 | 12 次匹配内核 QEMU 启动；未执行真实寄存器设置或 Secure Boot 验收 |
| 本机源码扩展 | 50 项 port 测试：37 通过，13 因 Windows 环境跳过 |

最终合计 **44/44** 云端任务。初轮失败来自共享测试默认值与 JSON 计数，已修正并重新运行完整应用矩阵。
研究工具及回放源码与成功研究提交 `ab172a8f37d8325c2d7dbae69ea4c5add4980322` 一致。

软件回归覆盖版本拒绝、身份变化、完整能力字及旧 MHz 冲突、值不变、固件拒绝、裁剪/丢弃、
写后版本或能力变化、I/O 失败和取消。GUI 检查读前禁止应用、确认期间和异步输入变化、过期快照、
成功回读与不匹配显示；CLI 独立进程解析完整 JSON 并验证原始值、null、单位及验证标志。
既有五项离线解析 ASan/UBSan 检查通过。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 为 Oracle Linux 7.9；Windows/macOS 只代表可移植核心验证。
每个 Linux 目标保存 134 张截图，共 1474 张；人工查看 EL7、Ubuntu 24.04 的成功与不匹配结果四张图。

295 个运行/构建文件逐一核对成功矩阵源码，11 个研究文件核对成功研究源码。
用户目录保留八项既有扩展和 UMC 文档别名。交付含 33 个 GUI/CLI/DKMS 包、
精确云端源码与 `updated-source/`（新增文档及本地扩展），全部文件由 `SHA256SUMS` 覆盖。

[功能与协议](amd-fmax-readback.md)、[完整验证记录](validation/amd-fmax-readback-ci-fca0725.json)、
[REA/固件证据](validation/amd-fmax-readback-research.json)。
本轮只验证固件保存的 Fmax 上限；真实时钟、稳定性和功耗效果未测量。
逐核/CCD 的字段意义、单位、有效掩码与物理映射，以及第二 rails 消费/回读仍未闭合。
运行时 `61h` 写入继续拒绝。
