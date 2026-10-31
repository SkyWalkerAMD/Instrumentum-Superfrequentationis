# Granite Ridge / 9950X3D 整批验证

2026-10-31，本轮为共享 GUI/CLI 增加三个**电压命令镜像**，以及核心第 8/11 行、CCD 第 7 行的平均代码值。
核心字段同时显示 CCD 存储槽位与局部核心槽位，保留原有平面槽位编号。
这些坐标尚未绑定物理 CCD 或 OS CPU；平均代码的具体含义也未确认。

目标为 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。
用户确认机器目前不在手边；真实 BIOS 未知，ASUS 2503 / SMU 98.84.0 是固定研究参考。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41174780925) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14，各 35 CTest、477 场景组；6 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41174792289) | 13/13；11 Linux 目标各 37 CTest、77 CLI 场景组、1547 JSON 报告；安装、卸载、重装、无 Qt/显示运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41175212695) | 26/26；各目标及 EL7/EL8 基线 237 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [REA 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41174811065) | 1/1；REA 6.7.0 实际分析两个 UEFI 模块；SMU/MP5 各 12240 项编码自检 |
| 新增通道回放 | 64 组覆盖/释放状态路径、12288 组直接写入路径（6144 组被覆盖标志锁定）、9 组超范围参数，4 次样本平均导出 |
| 新增代码字段回放 | 768 组四样本积累、13056 个输出字；覆盖所有源字节起始值、一/双槽、双槽的两个序号与分数平均 |
| 本机扩展检查 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

**44/44 云端任务通过**，核心、CLI 和 REA 验证提交 `5d5d4f92b90663da43b98a66361f2782c32817c0`。
GUI/安装包矩阵验证随后提交 `b46636f5fd459b4004edca0bf89cff6f1a121c56`，它仅将一处旧字段数断言从 20 改成 22；应用与构建代码不变。
旧 GUI 任务因该断言失败，保留于验证 JSON 的被替代任务记录，不计入通过数。
应用/构建文件与 16 个研究源文件逐一核对哈希。最终 main 发布提交只补充文档和证据。
全部任务 ID、下载产物摘要、源文件哈希及限制见验证 JSON。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 指 Oracle Linux 7.9；Windows/macOS 只验证核心。
生成 33 个安装包，每目标 143 张截图，共 1573 张。
直接检查 EL7 与 Ubuntu 24.04 的命令镜像、核心代码、CCD 代码视图，共六张合成数据截图；
屏幕外字段由 Qt 断言验证。QEMU 验证驱动集成，未验证物理寄存器调参或 Secure Boot。

PM 索引 48/53/58 来自三个通道的软件命令值，单位 V，不能作为实测电压或设置回读。
原索引 58 的 VDD_MISC 候选标签已移除。新发现的 0x11/0x13 覆盖命令分别选择通道 1/2，
0x12/0x14 只释放覆盖标志，不恢复之前的 VID。所选路径缺少参数范围检查，
三通道与主板物理 rails 的对应关系、设备效果及独立回读仍未确定；运行时不开放这些写入。

当前有十行核心、三行 CCD 的物理/无量纲单位证据，另有两行核心、一行 CCD 的代码表示证据；
公共区有十二个 SVI 遥测字段、三个命令镜像和七个未解释候选字段。
不能把代码表示数量算作已识别物理指标。字段新鲜度和目标固件来源仍未认证。

**仍未完成**：其余字段单位和代码含义、PM 到物理 CCD/OS 核心的可靠绑定、第二 rails 独立写入与回读，
以及既有清单中的剩余调参/其他固件适配。目标真机验收未执行。

交付目录 `dist/gui-cli-amd-granite-5d5d4f9/` 含 33 个安装包；根源码包对应精确云端提交，
`updated-source/` 包含用户扩展及最终研究/验证文档。`MANIFEST.json` 区分来源，
`SHA256SUMS` 覆盖全部交付文件。

[命令镜像、代码与存储坐标](amd-granite-channel-mirrors.md)、[计数器单位](amd-granite-counter-units.md)、
[SVI 遥测](amd-granite-svi-telemetry.md)、[映射证据](amd-granite-semantics.md)、
[验证清单](validation/amd-granite-ci-5d5d4f9.json)、[REA 证据](validation/amd-granite-research.json)。
