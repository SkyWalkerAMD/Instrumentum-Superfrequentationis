# Granite Ridge / 9950X3D APOB 与 OS CPU 核对验证

2026-11-01，继续使用 REA 6.7.0 / Ghidra 12.1.4 核对 ASUS ProArt
X870E-CREATOR WIFI BIOS 2503。GUI、CLI 与发行构建基于
[d50da8d](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/d50da8dd72735fa9beab77ac0ddcee28c68d6fd6)。
[后续研究提交](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/56475278681973db25a18fbcaf725c06a2e009e7)
只修改研究脚本、研究工作流和文档，应用／发行构建代码未变，另有通过的 REA 验证。
随后[补充界面导出](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/ca6eee9bb76cacb6b1f064609d92bcf0a82dffd3)
仅增加截图复制和复核工作流，逐字节确认 GUI、CLI、编译和打包输入未变。
最终发布提交仅增加文档和验证记录。

## 本轮实现

- GUI 加载 APOB 后可选择与可用 OS CPU 比较；CLI 提供
  `amd-topology --all-cpus --apob FILE [--l3]`，共用同一核对逻辑。
- 保留系统 CPU 编号及 APOB 原始位置/ID，列出参考 APIC 与 OS CPU 的
  对应、缺失 APIC 和未匹配 CPU；可附加 L3 容量及 CPUID 共享编号。
- 限定到已验证的 Model 44h / SMT2 拓扑，要求实际 type-3 叶；两遍
  CPU 清单与前后 CPUID 条件复核。冲突、读取失败或可用 CPU 集合变化
  均清空核对结果。GUI 保留已加载文件字节，改变磁盘文件不会悄然换输入。
- 原固件指令新增 48 组回放、624 条 APIC 记录，生成 24 组 / 312 行
  独立 C++ 对照数据。测试覆盖反向及稀疏 OS 编号、部分可用 CPU、缺项、
  不支持的拓扑、跨叶冲突、状态变化和每一次 CPUID 读取失败。
- 进一步追查更新时间基准，新增 8,246 组回放：SMU 的待处理门限可以跳过
  入队；MP5 先写确认、后尝试入队且不检查队列结果。确认回复不能证明模型已更新，
  这些指令也没有给出固定的绝对更新周期。详见[调度证据](amd-granite-scheduling.md)。

详见[核对功能与 REA 证据](amd-apob-os-comparison.md)。完整 APIC 集合匹配
仍不证明文件来源、运行时提供者或 PM／物理核心关联。
**本轮没有确认新的八行物理单位，没有完成 PM 物理绑定，也没有开放第二 rails 写入。**

## 最终验证结果

| 验证 | 结果 | 证据 |
|---|---|---|
| 可移植核心 | 4/4；每目标 37 个 CTest、504 组场景 | [core](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41308858007) |
| 无 GUI CLI | 13/13；11 个 Linux 目标均 39 个 CTest、84 组 CLI 场景、1,630 条夹具 JSON | [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41308863291) |
| GUI／发行包／内核 | 26/26；每个 Linux 目标 246 项 Qt 测试；33 个安装包、12 次内核虚拟机启动检查 | [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41308870664) |
| REA／固件回放 | 1/1；六个 UEFI 模块，SMU/MP5 各 12,240 组编码自检，28 组本地与云端回放结果一致 | [REA](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41310432044) |
| 新界面复核 | 2/2；EL7、Ubuntu 24.04 各 246 项 Qt 测试，补存新界面截图并验证来源 | [GUI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41311419177) |
| 合计 | **46/46 最终验证任务通过** | [完整记录](validation/amd-granite-ci-d50da8d.json) |

Linux 目标：el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13。EL7 是 Oracle Linux 7.9 基线。
Windows/macOS 仅运行核心测试；内核虚拟机检查不涉及真实调参寄存器或 Secure Boot。
发行矩阵还通过 142 项分析工具和 34 项移植检查；七组离线解析器的 ASan/UBSan 检查通过。
EL7/EL8 基线各通过 246 项 Qt 测试，主矩阵保留 1,650 张截图，另补存两张新核对界面截图，共 1,652 张。
本机源码镜像移植检查为 37 通过、13 项因工具/平台条件跳过；使用工作区 TEMP。
这 13 项同名检查已在云端逐项通过，并核对测试源码一致；详见记录中的 `local_skip_cloud_coverage`。
八项本机既有扩展保留，未作为整套扩展提交云端验证。

## 交付与边界

本机交付目录 `dist/gui-cli-amd-granite-d50da8d/`，含 11 组 GUI／CLI／DKMS 安装包。
`updated-source/octool-2.0.1-src.tar.gz` 含已验证代码、最终文档和八项既有扩展；
根目录源码包保留云端构建时快照。大小和校验值见 `MANIFEST.json`、`VALIDATION.json`
和 `SHA256SUMS`。EL7 与 Ubuntu 24.04 的新核对界面已人工查看。

仍未完成：核心 **0/4/12** 行、CCD **0/1/2/6/8** 行单位；PM 槽位到物理
CCD／OS 核心绑定；第二 rails 独立写入与设备回读。参考 APIC 核对提供中间
关联，不证明最终物理身份。目标机器不可用、实际 BIOS 未知；上述均不是目标真机验收。
