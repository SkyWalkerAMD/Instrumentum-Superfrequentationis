# Granite Ridge / 9950X3D 整批验证

2026-10-31，本轮为共享 GUI/CLI 增加核心第 9/10 行 **GHz 请求策略值**，以及全部可用 OS CPU 的 CPUID/L3 拓扑目录。
两行值沿 Fmax 的 MHz→GHz 缩放、SMU→MP5 记录、请求码合并和 PM 平均导出路径核对。
它们可能保留旧值，B 也可能大于 A，因此不显示为实测时钟、上下限或设置回读。

目标为 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。
目标机器不在手边；实际 BIOS 未知，ASUS 2503 / SMU 98.84.0 是固定研究参考。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41181010439) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14，各 35 CTest、479 场景组；6 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41181022766) | 13/13；11 Linux 目标各 37 CTest、78 CLI 场景组、1551 JSON 报告；安装、卸载、重装、无 Qt/显示运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41181115109) | 26/26；各目标及 EL7/EL8 基线 238 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [REA 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41180662273) | 1/1；REA 6.7.0 实际分析两个 UEFI 模块；SMU/MP5 各 12240 项编码自检 |
| 新增请求策略回放 | 13 组有限 Fmax 缩放、32768 组请求组合；8192 组双零输入保留旧值、1960 组 B 大于 A；一/双槽及双槽两个序号的四样本平均导出 |
| 新增拓扑检查 | 稀疏/交错 OS CPU ID、96/32 MiB L3 分组、每一步 CPUID 故障、重复 APIC、缓存不一致、第二遍变化、受限进程亲和性；失败时清空整个目录 |
| 本机扩展检查 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

**44/44 云端任务通过**。核心/CLI 提交 `938403fa4a59650734480efe157c0127b5ec0b91`；Linux GUI/安装包提交 `dd5f43239a349f8864c15407c61da16036782999`，
后者仅将移植测试包装器的预期场景组从 12 改为 14，应用与构建输入相同。
REA 提交 `6016964727519c1cbc179c5c8358a6e29047dccd` 的 17 个研究源文件与最终源一致；新增指令回放的云端 JSON 与本机结果逐项相同。
首轮 CLI 作用域错误、拓扑测试注入时机和旧场景数断言均已修正；被替代任务记录在验证 JSON，不计入通过数。
最终 main 发布提交只补充文档和证据。任务 ID、产物摘要、源文件哈希及限制见验证 JSON。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 指 Oracle Linux 7.9；Windows/macOS 只验证核心。
生成 33 个安装包，每目标 145 张截图，共 1595 张。
直接检查 EL7 与 Ubuntu 24.04 的请求策略和 CPU 拓扑视图，共四张合成数据截图；
视口之外字段由 Qt 断言验证。QEMU 验证驱动集成，未验证物理寄存器调参或 Secure Boot。

CLI `amd-topology --all-cpus --l3` 和 GUI 的全部可用 CPU 拓扑按钮采用同一核心实现。
目录保留 OS CPU 原编号，包含 APIC、Socket、CCD/Core/Thread 和 L3 几何，执行两轮 CPUID 并复核可用 CPU 集合。
它只涵盖当前进程允许访问的 CPU；两轮一致不代表原子快照，也不证明 PM 存储槽位或固件调参目标与物理核心对应。

当前有十二行核心、三行 CCD 的物理/无量纲单位证据，另有两行核心、一行 CCD 的代码表示证据；
公共区有十二个 SVI 遥测字段、三个命令镜像和七个未解释候选字段。
代码表示不等于已识别物理指标。字段新鲜度和目标固件来源仍未认证。

**仍未完成**：核心第 0/3/4/12 行、八行 CCD 字段及其余候选字段的含义/单位；
PM 到物理 CCD/OS 核心的可靠绑定；第二 rails 的物理身份、设备效果及独立设置回读；
以及既有清单中的剩余调参/其他固件适配。已有 0x11/0x13 通道覆盖路径和软件镜像不能替代硬件读回，运行时未开放这些写入。
目标真机验收未执行。

交付目录 `dist/gui-cli-amd-granite-dd5f432/` 含 33 个安装包；根源码包对应 Linux 云端验证提交，
`updated-source/` 包含用户扩展及最终研究/验证文档。`MANIFEST.json` 区分来源，`SHA256SUMS` 覆盖全部交付文件。

[请求策略与 CPU 目录](amd-granite-performance-policy.md)、[命令镜像及存储坐标](amd-granite-channel-mirrors.md)、
[计数器单位](amd-granite-counter-units.md)、[映射证据](amd-granite-semantics.md)、
[验证清单](validation/amd-granite-ci-dd5f432.json)、[REA 证据](validation/amd-granite-research.json)。
