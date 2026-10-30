# Granite Ridge / 9950X3D 整批验证

应用、GUI、CLI、核心验证使用 `091c1b40b7c8d7a8c6d0aeb51e9aa29bda77dff3`；REA 使用 `c54f05e45df70acbb41289c2c7e89c4cfe352cde`，11 个研究源文件哈希与交付一致。后续研究提交仅改动 rails 静态实验及其文档，应用输入未变。最终发布提交仅追加文档和证据。
目标为 Ryzen 9 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。
用户确认目标机器目前不在手边。已安装 BIOS 未知；ASUS 2503 / SMU 98.84.0 是固定研究参考，真机验收未执行。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41049339992) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14，各 35 CTest、477 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41049347145) | 13/13；11 Linux 目标各 37 CTest、77 CLI 场景组、1547 JSON 报告 |
| [GUI/内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41049354109) | 26/26；各目标与 EL7/EL8 基线 237 Qt 测试；142 分析测试、34 移植测试 |
| [REA/固件](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41050031099) | 1/1；REA 6.7.0 实际分析两个 UEFI 传输模块；SMU/MP5 各 12240 个编码自检 |
| 字段单位 | 384 组 CCD 传感器选择（192 组禁用后保留旧值）、3072 个初始选择器逐核最大值；512 个 VID 转换、2048 个 16 点电压网格、60 组插值；54 组计时器比值（18 组回绕） |
| 既有单位与布局 | 4096 个温度码、512 个逐核最大值、128 个失效缓存值、36 组驻留率、156 个归一化字；75 组映射、100 个启动包、15600 个 PM 复制字 |
| rails | 21432 组 61h 存储、18432 组共享下限；5120 组升降压比较、512 组直接路由、512 组锁定；0x90/0x91 各 7680 组参数/回包检查；未执行硬件写入或回读 |
| 内存检查 | 6 个离线解析/核心套件通过 ASan/UBSan |
| 安装与运行 | 11 目标 CLI 安装/卸载/重装、无显示/无 Qt 运行、非 root diagnose、GUI 显示；33 个安装包 |
| 内核集成 | 12 次匹配内核 QEMU 启动；未测试真实寄存器调参或 Secure Boot |
| 本地扩展 | 50 项：37 通过，13 因 Windows 缺工具链跳过；保留八项用户扩展 |

最终采用 **44/44 云端任务通过**。旧应用矩阵被 Model 44h 拓扑修正替代，其取消状态不计入通过数。每目标保留 137 张截图，共 1507 张；直接检查 EL7 和 Ubuntu 24.04
的 CCD 表及原始字视图。11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 指 Oracle Linux 7.9；Windows/macOS 仅验证可移植核心。

依据新取得的 Model 44h PPR，修复 CPUID 无 type 3 时被错误拒绝的问题：仅该型号用 Complex 层划分 CCD，并显示来源；该修复经完整应用矩阵验证。

GUI/CLI 新增解释核心 row 1 的电压模型（V），CCD row 3/4 的传感器最大值（deg C），
及 CCD row 11 的计时器比值（ratio）。加上既有温度/驻留率，共 **5 行核心、3 行 CCD**
具有参考固件的单位证据。数值直接使用固件已归一化结果；离线导入重新解释原始字节。
电压模型不表示实测电压，条件传感器可能保留旧值，比值的物理含义尚未确认，未标作 CPU 利用率。

61h 的索引参数参与共享下限；新增追踪确认升降压调用与直接调用在这些路径都指向通道 0。
0x90/0x91 则定位到另一套按通道/寄存器寻址的底层传输；未核实物理通道、寄存器契约与多字节一致性。软件镜像更新不能充当硬件回读。GUI/CLI 仍禁用尚未证实的 Granite rails 写入。

**仍未完成**：其他字段单位，PM 槽位到物理 CCD / OS 核心的可靠关联，第二 rails 独立写入/回读，
其他固件版本及既有清单中的剩余调参。已完成的两槽固件 ID 0/1 映射、CPUID L3 识别仍分别保留，
不会据此推断二者一一对应。真实传感器有效性和目标机器验收也未完成。

交付目录 `dist/gui-cli-amd-granite-091c1b4/` 包含 33 个安装包；根源码包对应精确云端提交，
`updated-source/` 保留用户扩展及最终文档。全部交付文件附 `SHA256SUMS`。

[协议与用法](amd-granite-pm.md)、[字段和映射依据](amd-granite-semantics.md)、
[验证清单](validation/amd-granite-ci-091c1b4.json)、[REA 证据](validation/amd-granite-research.json)。
