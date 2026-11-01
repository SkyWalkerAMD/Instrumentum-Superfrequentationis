# Granite Ridge / 9950X3D 拓扑兼容与 REA 验证

2026-11-01，本轮修复 GUI、CLI 对公开 9950X3D CPUID 记录的兼容问题，
并继续使用 REA 6.7.0 / Ghidra 12.1.4 核对 ASUS ProArt X870E-CREATOR WIFI BIOS 2503。
交付应用源码为 [aa40b23](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/aa40b23c78a02af72b1a9d13b03289fc2a2378e6)。
核心、CLI 和 REA 测试运行于 `cb4d810`；发行矩阵运行于 `aa40b23`。
两者仅相差移植测试中的场景总数断言（14 改为 16），应用、CLI 和研究输入完全一致。
截图复核版本为 `01c19a27b39bb4ba6ae73b8178cc0e573a3e32d2`，仅新增两条 PNG 导出语句并调整对应工作流。最终发布提交只更新文档和验证记录。

## 本轮实现

- 公开 9950X3D 记录同时报告 Complex（type 2）和 CCD（type 3），两者位移均为 4、
  逻辑处理器计数均为 16。之前直接拒绝 type 3，会使这种机器的拓扑读取失败。
- 共用解码器现接受边界、计数均一致的冗余层级，保留两层原始数据。
  JSON 增加 `redundant_ccd_level`，GUI 显示边界一致；缺失 type 3 时仍使用原有回退路径。
  层级冲突、缺失 Complex、读取失败时不返回可用映射。
- 新增全部 32 个捕获 APIC 的读取检查、96/32 MiB L3 区分、稀疏且反向 OS CPU 编号、
  全部读取故障位置、单核和整组结果清理检查。GUI、CLI 同步覆盖成功与冲突场景。
- REA 已解析的原始 APIC 消费者、排序器、两套提供者和构造器执行
  **512 组公开 CPUID 输入回放，检查 16,384 条输出记录**。
  原有 864 组枚举回放结果保持一致。

公开记录来自 **ASUS ROG Strix X670E-E / BIOS 2904**，不是用户的 X870E Creator。
APOB 及固件服务仍为模拟输入，终止 CPUID 子叶也未出现在公开记录中。
详见[来源、修复与证据范围](amd-granite-published-topology.md)。
**本轮未新增八行中的任何物理单位，未建立 PM 到物理/OS 核心绑定，也未开放第二 rails 独立写入。**

## 最终验证结果

| 验证 | 结果 | 证据 |
|---|---|---|
| 可移植核心 | 4/4；Ubuntu 22.04/24.04、Windows 2022、macOS 14；每目标 37 个 CTest、498 组场景 | [core](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41295129998) |
| 无 GUI CLI | 13/13；11 个 Linux 目标均 39 个 CTest、82 组 CLI 场景、1,618 条夹具 JSON | [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41295134383) |
| GUI／发行包／内核 | 26/26；各 Linux 目标 245 项 Qt 测试，EL7/EL8 基线各 245 项；33 个安装包、12 次内核虚拟机启动检查 | [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41295371192) |
| REA／固件回放 | 1/1；六个 UEFI 模块，SMU/MP5 各 12,240 组编码自检，23 组本地与云端回放结果一致 | [REA](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41295143730) |
| 截图复核 | 2/2；EL7、Ubuntu 24.04 各重跑 245 项 GUI 测试，验证应用／编译输入一致并导出新截图 | [GUI 复核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41297234692) |
| 合计 | **46/46 最终任务通过** | [完整机器可读记录](validation/amd-granite-ci-aa40b23.json) |

Linux 目标：el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13。EL7 为 Oracle Linux 7.9 基线。
Windows/macOS 是核心测试，不是 Linux 安装包或真机调参验收。
内核虚拟机检查未覆盖真实调参寄存器或 Secure Boot。
发行矩阵还通过 142 项分析工具和 34 项移植检查；七组离线解析器的 ASan/UBSan 检查通过。
本机分析检查为 139 通过、3 跳过；本机源码镜像的移植检查为 37 通过、13 跳过。
本机初次检查未配置隔离依赖和可用临时目录，配置后重新运行通过。
八项既有本机扩展单独保留，未作为整套扩展提交云端验证。

首轮发行矩阵因外围测试仍要求 14 组场景而失败，实际拓扑程序已通过新增后的 16 组。
修正该断言后重跑发行验证；与此文件无关的核心、CLI、REA 运行保留，并核对输入一致性。
被替代运行及原因保存在验证记录中，没有计入最终通过数量。

原发行脚本未导出已生成的新增截图，已补上两条复制语句。补充验证只重跑两个代表环境，
确认应用、编译及测试输入一致。主矩阵保留 1,639 张截图，补充保留 2 张，共 1,641 张。

## 交付与边界

本机交付目录 `dist/gui-cli-amd-granite-aa40b23/` 含 11 组 GUI／CLI／DKMS 安装包。
`updated-source/octool-2.0.1-src.tar.gz` 包含已验证代码、最终文档及八项既有用户扩展；
根目录源码包保留云端发行测试时的原始快照。来源、字节数、哈希和检查范围见
`MANIFEST.json`、`VALIDATION.json` 与 `SHA256SUMS`。
本机源码镜像已核对，保留既有扩展和 UMC 文档链接别名。
EL7 和 Ubuntu 24.04 的新增 9950X3D 拓扑截图已检查，哈希保存在验证记录中。

仍未完成：核心第 **0/4/12** 行、CCD 第 **0/1/2/6/8** 行单位；PM 槽位与物理
CCD／OS 核心绑定；第二 rails 独立写入与设备回读。
公开 CPUID 和原指令回放不能补齐计数事件、时间基准、模型系数校准或实际固件配置。
目标机器不在用户身边，安装的 BIOS 未知，本轮没有进行真机验收。
