# Granite Ridge / 9950X3D Fmax 验证

2026-11-01，GUI 和 CLI 同步新增 Granite Ridge Fmax 保存上限读取、比较旧值、设置和写后回读。精确限定 SMU 98.84.0；固件限幅或回读不同会报告未验证，不自动重试、切换模式或回滚。读取单位为 MHz，实际运行频率未测量。

目标为 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。用户确认机器不在手边，实际 BIOS 未知；ASUS 2503 是研究参考，目标真机验收尚未执行。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41272403084) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 36 CTest、488 场景组；6 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41273485165) | 13/13；11 Linux 目标各 38 CTest、80 CLI 场景组、1572 JSON 报告；安装、卸载、重装、无 Qt/显示运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41272412665) | 26/26；各目标及 EL7/EL8 基线 241 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [Granite 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41272418086) | 1/1；REA 6.7.0 分析两个 UEFI 模块；SMU/MP5 各 12240 项编码自检；15 项本地/云端回放结果一致 |
| [原版 REA 复核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41271795943) | 1/1；原版 Fmax、构造函数与调用者的 pbo_layout 静态分析；保留调用约定和终止流限制 |
| [截图补充复核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41273776159) | 2/2；EL7、Ubuntu 24.04 各 241 Qt 测试；核对 GUI/构建/测试输入与原矩阵相同，补充收集新增 Fmax 图像 |
| Fmax 策略回放 | 2496 组；2179 接受、317 拒绝、1669 次限幅或哨兵处理、179 次条件降频接受；硬件接口为合成输入 |
| 能力位回放 | 4204 组；检查 bit 2、手动 OC bit 16、前置门控及其他位保留 |
| 应用回归 | 身份、版本、六参数清零、旧快照、能力变化、固件拒绝、限幅、超时、各读取故障点、取消及跨平台误用；同一核心供 GUI/CLI 使用 |
| 本机扩展检查 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

**47/47 最终云端任务通过。** 核心、GUI/内核和 Granite 固件工作流使用实现提交 `5d12a19212292d7fae3a17fbcbde408d6c31ea16`。
CLI 工作流使用 `e58010d45957787871954e9645484e0fe7454839`：前两次运行分别发现报告总数断言未更新（1551 → 1572），以及 Scalar 结果筛选混入新增的非法平台参数拒绝。只更正这两项测试断言后重跑 CLI 矩阵，拒绝结果和设备回读分别检查。运行代码、构建脚本和其他测试输入均未改变；失败日志与替代运行保留在验证记录中。
原版 REA 工作流使用上一发布提交 `310d04cae43e706a91a3deb559aeebf80717dec8`，其工作流和分析脚本与本轮一致，分别核对哈希。22 个 Granite 研究源文件与通过版本一致。
最终 main 提交只补充文档和证据，应用与构建输入保持通过版本。

本机第一次分析测试因缺少 Capstone/Unicorn 无法加载，已由云端完整运行；移植测试的首次临时目录权限问题通过使用工作区内临时目录解决。记录为环境限制，不计作通过的本地测试。
任务、源文件哈希、产物摘要和限制见[验证 JSON](validation/amd-granite-ci-5d12a19.json)及[原版 REA 证据](validation/amd-granite-fmax-native.json)。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。EL7 指 Oracle Linux 7.9；Windows/macOS 只验证核心。
生成 33 个安装包，主矩阵每目标保留 147 张截图，共 1617 张；另补充导出 4 张 Fmax 截图。直接检查 EL7 与 Ubuntu 24.04 的 Granite Fmax 成功回读和原有 Shimada Fmax 回读视图；均为合成设备场景。
QEMU 验证驱动集成，未验证物理寄存器调参或 Secure Boot。

当前仍有核心 0/4/12、CCD 0/1/2/6/8 的物理单位未确认；PM 到物理 CCD/OS 核心绑定、第二 rails 的物理身份与独立写入/设备侧回读、其他未还原调参及其他固件适配仍未完成。已有模型、软件镜像与共享 Fmax 保存值不能替代真机测量。

交付目录 `dist/gui-cli-amd-granite-5d12a19/` 含 33 个安装包。推荐使用 `updated-source/`：包含通过 CLI 复测的两项断言更正、用户扩展与最终文档。根源码包以保留的 GUI/内核云端原始快照为基础，仅加入通过复测的两项测试断言更正，并逐文件核对与 CLI 通过提交一致；此本地重建过程及前后哈希单独记录。`MANIFEST.json` 区分来源，`SHA256SUMS` 覆盖全部交付文件。

[Fmax 协议与用法](amd-granite-fmax.md)、[Granite 研究证据](validation/amd-granite-research.json)、[八行剩余来源](amd-granite-producer-paths.md)。
