# Granite Ridge / 9950X3D 整批验证

本轮为共享 GUI/CLI 增加三条 SVI 固件通道的电压、电流、功率、温度，共 **12 个公共 PM 字段**。
单位分别为 V、A、W、deg C，表内数据已平均，应用不再缩放。
目标为 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。
用户确认机器目前不在手边；真实 BIOS 未知，ASUS 2503 / SMU 98.84.0 仅作为固定研究参考。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41057093230) | 4/4，Ubuntu 22/24、Windows 2022、macOS 14；各 35 CTest、477 场景组；6 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41058252731) | 13/13；11 Linux 目标各 37 CTest、77 CLI 场景组、1547 JSON 报告；安装、卸载、重装、无 Qt/显示运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41057819927) | 26/26；各目标及 EL7/EL8 基线 237 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [REA 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41057598730) | 1/1；REA 6.7.0 实际分析两个 UEFI 模块；SMU/MP5 各 12240 项编码自检 |
| 新增遥测回放 | 49152 个通道转换；256 个量程回复、768 个初始化存储；全部 512 种有效性掩码两轮、9216 个载荷、4608 个旧值保留检查；12 字段完整累加/平均/输出路径 |
| 新增寄存器回放 | 6912 次读助手、6912 次写助手、11 个停滞等待状态；全部使用脚本化 MMIO 回复，未执行真实硬件读写 |
| 新增核心索引回放 | 全部 256 种核心启用掩码，2048 个计数器复制、4096 个禁用槽旧值保留；仍未关联 CPUID/APIC/OS 核心 |
| 本机扩展检查 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

采用 **44/44 云端任务通过**。核心提交 `91854d32b98e32d1feea6faf3bed3188ee5b0dc4`，GUI/内核提交 `27daaee7cec2b9545d22c69657bf9697f9817c61`，
CLI 提交 `edc8a7def223f4ad3f7060c5d594def5f28caef8`，REA 提交 `d3f301989cb2aeb2109648f0795a36543dd66b49`。
后续 GUI 和 CLI 提交分别修正一处遗留测试断言（旧 8 行、全部单位未知），生产代码保持一致。
各矩阵测试自己的对应提交，所有应用输入及 13 个研究源文件分别核对哈希。
被替代的失败/取消任务完整记录在验证 JSON 中，不计作通过任务。
最终 main 发布提交只增加文档和证据。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 指 Oracle Linux 7.9；Windows/macOS 只验证核心。
共生成 33 个安装包，每目标 138 张截图，共 1518 张。
直接检查 EL7 和 Ubuntu 24.04 的 SVI、CCD、原始字视图，共六张合成数据截图；
屏幕外的表格行由 Qt 断言验证。QEMU 验证驱动集成，未验证物理寄存器调参或 Secure Boot。

本地首次移植测试使用系统沙箱临时目录时遇到权限错误；改用工作区临时目录后完成检查。
本轮所有可用本机和云端检查已完成，**目标真机验收未执行**。

SVI 读取失败可能保留旧值，电流量程模式 0 可能产生零电流/零功率；这些 PM 字段不携带有效性标记。
GUI 与 CLI 明示新鲜度和物理电压轨身份未验证。它们不能作为第二 rails 的独立设置回读。
既有五行核心、三行 CCD 单位、Model 44h CPUID/L3 识别和精确双槽固件 ID 映射继续保留。

**仍未完成**：其余字段单位、PM 槽位到物理 CCD/OS 核心的可靠绑定、第二 rails 独立写入和回读，
以及既有清单中的剩余调参/其他固件适配。新增底层编码与等待循环证据没有启用未知写入命令。

交付目录 `dist/gui-cli-amd-granite-edc8a7d/` 含 33 个安装包；根源码包对应 GUI 云端提交，
`updated-source/` 包含最终测试修正、用户扩展及发布文档。`MANIFEST.json` 区分验证来源，
`SHA256SUMS` 覆盖全部交付文件。

[遥测字段与限制](amd-granite-svi-telemetry.md)、[核心/CCD 映射依据](amd-granite-semantics.md)、
[验证清单](validation/amd-granite-ci-edc8a7d.json)、[REA 证据](validation/amd-granite-research.json)。
