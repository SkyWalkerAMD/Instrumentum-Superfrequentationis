# Granite Ridge / 9950X3D 整批验证

2026-10-31，本轮为共享 GUI/CLI 增加 **五行核心计数器单位**，并纠正 CCD 第 11 行的百分比解释。
核心第 13/14 行为 ratio，第 15/16/17 行为 %；CCD 第 11 行在固件最终输出前乘以 100，单位应为 %。
应用保留原始数值，不再缩放或夹紧。当前共解释十行核心、三行 CCD 和十二个 SVI 公共字段的单位。
计数器事件含义仍未知，字段名称与状态保留这一限制。

目标为 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。
用户确认机器目前不在手边；真实 BIOS 未知，ASUS 2503 / SMU 98.84.0 仅为固定研究参考。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41168735473) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14，各 35 CTest、477 场景组；6 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41168741626) | 13/13；11 Linux 目标各 37 CTest、77 CLI 场景组、1547 JSON 报告；安装、卸载、重装、无 Qt/显示运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41168748247) | 26/26；各目标及 EL7/EL8 基线 237 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [REA 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41168754187) | 1/1；REA 6.7.0 实际分析两个 UEFI 模块；SMU/MP5 各 12240 项编码自检 |
| [GUI 截图补验](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41170359762) | 2/2；EL7 与 Ubuntu 24.04 各 237 Qt 测试，断言目标行确实在视口顶部，补充六张截图 |
| 最终 PM 输出 | 18 次完整归一化/输出，2808 个字；不跳过倒数返回后的浮点指令，覆盖迟至输出阶段的 CCD 百分比缩放 |
| 新增计数器回放 | 750 组事件百分比、384 组比例分配、300 组量化百分比；含回绕及零分母路径；8 次源字段到输出连接检查 |
| 直接 VID 命令回放 | 9216 组封包与等待路径、2 个停滞状态；只有完成标志，没有电压回读载荷，未进行硬件写入 |
| 本机扩展检查 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

采用 **46/46 云端任务通过**。原 44 项应用和 REA 验证同一提交 `36cc2bafdd54b957fdf67d85346eb124d371c3b0`。
随后提交 `e39b711e7de2966d512eecb843c174acc2970b47` 仅修正测试截图定位和证据工作流，应用代码与样本值不变，另两项补验通过。
296 个应用/构建文件及 14 个研究源文件的哈希与相应成功任务核对；其中两个测试/工作流文件采用补验提交。
最终 main 发布提交只增加文档和证据；历史错误的 ratio 标签在旧报告中保留，本轮明确纠正。
首轮共享测试变量 `slots` 与 Qt 宏冲突，重命名后重新运行全部矩阵。
被替代的失败/取消任务保留在验证 JSON 中，不计入通过数。
原比例截图在滚动范围更新前取景，未显示目标行；保留原产物，并采用补验截图。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 指 Oracle Linux 7.9；Windows/macOS 只验证核心。
共生成 33 个安装包，每目标 141 张截图，共 1551 张，另有补验六张。
直接检查 EL7 和 Ubuntu 24.04 的核心比例、核心百分比及 CCD 百分比视图，共六张合成数据截图；
屏幕外表格行由 Qt 断言验证。QEMU 验证驱动集成，未验证物理寄存器调参或 Secure Boot。

字段单位修正不会认证计数器事件含义、实时新鲜度或物理核心身份。
核心第 17 行的零分母会保留旧值；SVI 也可能保留旧值或在量程模式 0 下产生零电流。
新增 VID 封包证据没有建立第二 rails 独立设置回读，因此未开放该写入协议。

**仍未完成**：其余字段单位、PM 槽位到物理 CCD/OS 核心的可靠绑定、第二 rails 独立写入和回读，
以及既有清单中的剩余调参/其他固件适配。目标真机验收未执行。

交付目录 `dist/gui-cli-amd-granite-36cc2ba/` 含 33 个安装包；根源码包对应精确云端提交，
`updated-source/` 包含用户扩展及最终研究/验证文档。`MANIFEST.json` 区分来源，
`SHA256SUMS` 覆盖全部交付文件。

[计数器字段与修正](amd-granite-counter-units.md)、[SVI 字段](amd-granite-svi-telemetry.md)、
[映射依据](amd-granite-semantics.md)、[验证清单](validation/amd-granite-ci-36cc2ba.json)、
[REA 证据](validation/amd-granite-research.json)。
