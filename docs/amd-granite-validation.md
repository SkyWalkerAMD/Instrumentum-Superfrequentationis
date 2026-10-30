# Granite Ridge / 9950X3D 整批验证

GUI/CLI/安装包验证提交：`8a8f3c967cfad0dff52d5a61a65d1a7e62c18a86`。核心验证：`940bd99ce8a47efd2b1f062205381ca79d8e4148`。
REA 研究验证：`22faac9ca8c856d57ea4f31553876eabcb1a38c6`。后两次应用提交仅修正测试中的 Qt 宏冲突和平台布局假设；
运行时代码逐文件一致。后续 rails 研究增加静态回放和文档，未开放硬件写入。

用户目标为 **Ryzen 9 9950X3D + ASUS X870E Creator**，主板按 ProArt X870E-CREATOR WIFI
匹配。已安装 BIOS 版本未知；ASUS 2503 只是固定的公开研究输入。本轮未进行目标机器的真机验收。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41000182974) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14，各 35 CTest、471 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41000829113) | 13/13；11 Linux 目标各 37 CTest、75 CLI 场景组、1539 JSON 报告 |
| [GUI/内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41000829129) | 26/26；每个目标与 EL7/EL8 基线各 235 Qt 测试；142 分析测试、34 移植测试 |
| [REA/固件](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41001072012) | 1/1；3 个完整存储布局回放、20 个元数据回放、21432 个 61h 存储案例；SMU/MP5 各 11472 个合成编码自检 |
| 内存检查 | 6 个离线解析/核心套件通过 ASan/UBSan，包括新增 Granite 路径 |
| 安装与运行 | 11 个目标完成无桌面 CLI 安装/卸载/重装、非 root diagnose 和 GUI 显示测试 |
| 内核集成 | 12 次匹配内核的 QEMU 启动；未执行真机寄存器调参或 Secure Boot 验收 |
| 本地源码扩展 | 50 项测试：37 通过，13 因 Windows 环境跳过；保留八项原有扩展 |

最终采用 **44/44** 成功任务。早期 Qt 宏冲突及 CLI 布局断言失败的任务未计入，
也未用跳过测试处理失败。每个 Linux 目标保留 137 张截图，共 1507 张；人工查看
EL7、Ubuntu 24.04 的新增 CCD 存储及完整表视图。完整任务、artifact 摘要和源码比较见机器可读报告。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 指 Oracle Linux 7.9；Windows/macOS 只验证可移植核心，未声明完整应用移植。

本轮完成独立 Granite Ridge 原生 PM 读取、两种精确表长度、18 行逐核与 12 行 CCD
存储区、完整原始表检查、跨 GUI/CLI 快照导入导出和错误处理。原生读取限定
1Ah/44h、PCI 1022:14d8、SMU 98.84.0 以及 620105/1828 或 620205/2452。
未知固件、表长不符、地址越界、元数据变化或读取失败均拒绝发布缓存。

**仍未完成**：字段物理单位、缓存 CCD/频率 CCD 与 OS 核心的对应关系、第二 rails 的消费路径与
可靠回读、其它固件版本和剩余调参。61h 的参数/限幅/存储回放不等于安全可用的物理电压写入。
A5 不按传入 CCD 参数选目标，6Eh 也不是该 halfword 数组的直接回读；GUI/CLI 不开放 Granite rails 写入。

交付目录：`dist/gui-cli-amd-granite-8a8f3c9/`，包含 33 个 GUI/CLI/DKMS 安装包、
精确云端源码包，以及保留用户扩展和最终研究文档的 `updated-source/`。所有交付文件由 `SHA256SUMS` 校验。

[协议与用法](amd-granite-pm.md)、[应用验证](validation/amd-granite-ci-8a8f3c9.json)、
[REA 研究证据](validation/amd-granite-research.json)。
