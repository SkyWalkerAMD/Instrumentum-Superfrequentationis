# Granite Ridge / 9950X3D 拓扑候选目标与 CPU 入口验证

2026-11-01，本轮继续用 REA 6.7.0 / Ghidra 12.1.4 核对 ASUS ProArt
X870E-CREATOR WIFI BIOS 2503，并同步增加 GUI 和 CLI 的离线拓扑诊断。
应用与发行构建基于
[9e21a56](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/9e21a56a9f08bbaece5dd7efd947b0e4cfc805bf)；
后续 [REA 研究提交](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/7de4b75616f2e904745f91f92e677bdac39af4ba)
仅修改研究脚本、研究工作流和文档，应用与发行构建输入未变。
最终发布提交仅增加文档和验证记录。

## 本轮实现

- 3,244 组原始指令回放确认：两套 APOB 提供者分别用组 ID 和子组 ID
  计算寄存器目标，位号由首个核心的线程标志前缀和所选核心的枚举序号决定。
  不同 ID、不同枚举条目仍可能产生相同地址与位号。
- GUI 和 CLI 共用的离线解码器增加首项线程前缀、前缀不一致数量、两套候选目标
  及重复目标诊断。保留原始 ID 和标志；解析失败时清空结果，不生成调参命令。
- 继续沿固件入口核对 CPU 条件，新增 16,636 组原始指令回放。
  项目中的 9950X3D 夹具特征值 `0x00b40f40`、EBX 高四位为零时，
  通过 A 路径而不通过 B 路径的入口条件。这缩小了参考路径，但没有验证
  用户实际 BIOS 的协议安装、运行时提供者或寄存器的物理作用。
- REA 增加入口、CPUID 包装函数和 B 比较函数的分析；最终 A/B 分别包含
  17/18 个显式分析入口。所有回放使用模拟输入，在固件服务前停止。

详见[寄存器候选目标及入口条件证据](amd-apob-register-targets.md)。
产品仍展示两个候选路径，不据此选定写入目标。
**本轮没有确认新的八行物理单位，没有建立 PM 到物理/OS 核心绑定，也没有开放硬件写入。**

## 最终验证结果

| 验证 | 结果 | 证据 |
|---|---|---|
| 可移植核心 | 4/4；每目标 37 个 CTest、498 组场景 | [core](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41303599941) |
| 无 GUI CLI | 13/13；11 个 Linux 目标均 39 个 CTest、82 组 CLI 场景、1,618 条夹具 JSON | [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41303604913) |
| GUI／发行包／内核 | 26/26；每个 Linux 目标 245 项 Qt 测试；33 个安装包、12 次内核虚拟机启动检查 | [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41303610823) |
| REA／固件回放 | 1/1；六个 UEFI 模块，SMU/MP5 各 12,240 组编码自检，26 组本地与云端回放结果一致 | [REA](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41304365186) |
| 合计 | **44/44 最终验证任务通过** | [完整记录](validation/amd-granite-ci-9e21a56.json) |

Linux 目标：el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13。EL7 是 Oracle Linux 7.9 基线。
Windows/macOS 仅运行核心测试；内核虚拟机检查不涉及真实调参寄存器或 Secure Boot。
发行矩阵还通过 142 项分析工具和 34 项移植检查；七组离线解析器的 ASan/UBSan 检查通过。
EL7/EL8 基线各通过 245 项 Qt 测试，主矩阵保留 1,650 张截图。
本机源码镜像的移植检查为 37 通过、13 项因工具/平台条件跳过；使用工作区内专用
TEMP 目录。八项本机既有扩展保留，未作为整套扩展提交云端验证。

## 交付与边界

本机交付目录为 `dist/gui-cli-amd-granite-9e21a56/`，含 11 组 GUI／CLI／DKMS 安装包。
`updated-source/octool-2.0.1-src.tar.gz` 包含已验证代码、最终文档和八项既有扩展；
根目录源码包保留应用云端构建时快照。来源、大小和校验值见 `MANIFEST.json`、
`VALIDATION.json` 和 `SHA256SUMS`。EL7 与 Ubuntu 24.04 的拓扑诊断界面已人工查看。

仍未完成：核心第 **0/4/12** 行、CCD 第 **0/1/2/6/8** 行单位；PM 槽位与物理
CCD／OS 核心绑定；第二 rails 独立写入与设备回读。
剩余证据缺口是计数事件、绝对时间基准、模型系数校准、运行时拓扑关联及设备侧协议。
内部原始读取函数尚未找到主机可达路径，不能将其作为第二 rails getter。
用户机器不在身边，安装的 BIOS 未知，参考固件回放不能视为目标真机验收。
