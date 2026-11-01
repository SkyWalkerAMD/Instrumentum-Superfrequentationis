# Granite Ridge / 9950X3D 配置端口与保留值验证

2026-11-01，本轮继续用 REA 6.7.0 / Ghidra 12.1.4 核对 ASUS ProArt
X870E-CREATOR WIFI BIOS 2503 / MP5 98.84.0，并同步修正 GUI 和 CLI。
应用、研究、发行构建均基于同一提交
[ee1aee1](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/commit/ee1aee1d0f6f1fa1e574f629d95717fdbd86e59b)。
最终发布提交仅增加文档和验证记录，应用输入未再修改。

## 本轮实现

- 原始指令回放确认内部配置写入顺序、原始字节读取握手，以及写入后的三次固定等待。
  6,144 组写入与 6,144 组读取组合通过；另覆盖 16 组包装函数参数、
  三组超宽编码、计时器停滞和回复标志缺失。
- 126 组状态分支回放发现 45 组“配置码已改变、PM 浮点值仍旧保留”的情况。
  该分支完整复制配置记录，但不执行配置端口读写。模式字节含义尚未确认。
- GUI 将 CCD 第 2 行显示为可能保留旧值的配置镜像；单位仍标为未验证。
  CLI 同步给出保留值、未检查设备应用结果、未验证主机读取路径等信息。
  导入捕获文件时丢弃伪造的回读、成功状态和单位声明，重新生成这些字段。

详见[配置端口与保留值证据](amd-granite-config-port.md)。内部原始读取函数的存在
不能证明可用的主机 getter、读写配对或第二 rails。计时器和回复为模拟输入。
**本轮未新增八行中的任何物理单位，未建立 PM 到物理/OS 核心绑定，未开放新的硬件写入。**

## 最终验证结果

| 验证 | 结果 | 证据 |
|---|---|---|
| 可移植核心 | 4/4；每目标 37 个 CTest、498 组场景 | [core](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41299927524) |
| 无 GUI CLI | 13/13；11 个 Linux 目标均 39 个 CTest、82 组 CLI 场景、1,618 条夹具 JSON | [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41299933460) |
| GUI／发行包／内核 | 26/26；每个 Linux 目标 245 项 Qt 测试；33 个安装包、12 次内核虚拟机启动检查 | [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41299941974) |
| REA／固件回放 | 1/1；六个 UEFI 模块，SMU/MP5 各 12,240 组编码自检，24 组本地与云端回放结果一致 | [REA](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41299951002) |
| 合计 | **44/44 任务通过** | [完整记录](validation/amd-granite-ci-ee1aee1.json) |

Linux 目标：el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13。EL7 是 Oracle Linux 7.9 基线。
Windows/macOS 仅运行核心测试；内核虚拟机检查不涉及真实调参寄存器或 Secure Boot。
发行矩阵还通过 142 项分析工具和 34 项移植检查；七组离线解析器的 ASan/UBSan 检查通过。
EL7/EL8 基线各通过 245 项 Qt 测试，主矩阵保留 1,650 张截图。
本机源码镜像的移植检查为 37 通过、13 项因工具/平台条件跳过；临时目录限制通过
工作区内专用 TEMP 目录解决。八项本机既有扩展保留，未作为整套扩展提交云端验证。

## 交付与边界

本机交付目录为 `dist/gui-cli-amd-granite-ee1aee1/`，含 11 组 GUI／CLI／DKMS 安装包。
`updated-source/octool-2.0.1-src.tar.gz` 包含已验证代码、最终文档和八项既有扩展；
根目录源码包保留云端构建时快照。来源、大小和校验值见 `MANIFEST.json`、
`VALIDATION.json` 和 `SHA256SUMS`。EL7 与 Ubuntu 24.04 的配置镜像界面已人工查看。

仍未完成：核心第 **0/4/12** 行、CCD 第 **0/1/2/6/8** 行单位；PM 槽位与物理
CCD／OS 核心绑定；第二 rails 独立写入与设备回读。
剩余证据缺口是计数事件、绝对时间基准、模型系数校准、运行时拓扑关联及设备侧协议。
用户机器不在身边，安装的 BIOS 未知，不能将参考固件回放称为目标真机验收。
