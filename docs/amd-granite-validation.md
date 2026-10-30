# Granite Ridge / 9950X3D 整批验证

GUI、CLI 和 REA 验证于应用提交：`5ff594b3ecd905a036a020645b4fd0a06ae965dd`。
核心四个平台验证于 `25895adba1a6c86867042cbc8e8ed0ca91fd28c4`；唯一差别是缓存拓扑测试在重新分配 vector 前复制填充值，
修复 macOS 暴露的测试构造问题，原有断言保持不变。应用与研究输入一致。
最终发布提交仅追加文档与证据清单。

目标为 **Ryzen 9 9950X3D + ASUS X870E Creator**，主板按 ProArt X870E-CREATOR WIFI
匹配。已安装 BIOS 未知；ASUS 2503 / SMU 98.84.0 为固定公开研究输入，未冒充真机结果。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41026404979) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14，各 35 CTest、477 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41023307096) | 13/13；11 Linux 目标各 37 CTest、77 CLI 场景组、1547 JSON 报告 |
| [GUI/内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41023307161) | 26/26；每个目标与 EL7/EL8 基线各 237 Qt 测试；142 分析测试、34 移植测试 |
| [REA/固件](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41023307189) | 1/1；SMU/MP5 各 12240 个编码自检，75 组初始化映射、100 个启动包、15600 个 PM 写入字 |
| 单位与 rails 回放 | 4096 个温度码、512 个逐核最大值、128 个失效传感器缓存值、36 组驻留率、156 个归一化字；21432 个 61h 存储及 18432 个共享下限案例 |
| 内存检查 | 6 个离线解析/核心套件通过 ASan/UBSan |
| 安装与运行 | 11 目标无桌面 CLI 安装/卸载/重装、非 root diagnose、GUI 显示；33 个 GUI/CLI/DKMS 安装包 |
| 内核集成 | 12 次匹配内核 QEMU 启动；未测试真实寄存器调参或 Secure Boot |
| 本地扩展 | 50 项检查：37 通过，13 因 Windows 缺工具链跳过；保留八项用户扩展 |
| 初始传感器索引 | 本地 Python 单独回放固件初始索引：64 组、512 个逐核最大值；未计入云端任务或真机结果 |

最终采用 **44/44** 成功任务。修正过的早期缺头文件、测试计数及 macOS 测试构造失败未计入；
其后通过的中间版本也被完整的新功能矩阵替代。每目标保留 137 张截图，共 1507 张；
查看了 EL7、Ubuntu 24.04 的固件 CCD 映射与完整表视图。详细哈希及文件比较见验证 JSON。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。
EL7 指 Oracle Linux 7.9。Windows/macOS 仅验证可移植核心，不代表完整应用移植。

本轮 GUI/CLI 同步完成：

- 逐核温度单位及三组驻留百分数；保存固件已转换的数值，避免二次缩放。驻留状态名称仍标为候选。
- `amd-topology --cpu N --l3`：读取 CPUID CCD/核心与 L3 几何，支持比较两个 CCD 的缓存容量。
- `amd-pm-native-read --cpu N --platform granite-ridge --map-ccd`：在精确 SMU 98.84.0 / 0x620205 条件下，
  依据初始化器把两个存储槽映射至固件目标 0/1。单槽不能推断，映射请求会拒绝；普通单槽读取仍可用。
- 失效、元数据变化、取消及离线导入检查；映射结果不作为 OS 核心或 CPUID CCD 的对应证据。

**仍未完成**：其余字段单位、PM 槽位到 CPUID/OS 核心的物理关联、独立第二 rails 的写入与回读、
其它固件版本及既有清单中的剩余调参。61h 的第二存储项可参与共享下限计算；独立物理供电轨身份、
编码硬件请求与可靠 getter 尚未闭合，GUI/CLI 继续禁用 Granite rails 写入。没有目标机器的真机验收。

交付目录：`dist/gui-cli-amd-granite-5ff594b/`。根源码包对应精确云端提交；`updated-source/`
保留用户扩展和最终文档。33 个安装包及所有源码、证据清单通过 `SHA256SUMS` 校验。

[协议与用法](amd-granite-pm.md)、[单位与映射](amd-granite-semantics.md)、
[应用验证](validation/amd-granite-ci-5ff594b.json)、[REA 证据](validation/amd-granite-research.json)。
