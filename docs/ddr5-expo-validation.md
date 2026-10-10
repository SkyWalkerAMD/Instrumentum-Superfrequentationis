# DDR5 EXPO：GUI 与 CLI 同步验证

生产提交 `e4cb81bb71f2d5c871ab160317d8ed91effbd9a7`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/ddr5-expo-ci-e4cb81b.json)绑定同一提交的运行、日志、安装包、源码和 SHA256。
[实现与边界](ddr5-expo-recovery.md)记录 EXPO 1.0 字节布局、来源及错误隔离。

两端同步读取两组基础档案，各含三项电压与十项存储时序。
EXPO 使用整块 CRC，与 XMP 各区分别报告；不依赖 XMP 存在或 XMP 头正确。
禁用档案不解码残留数据，已启用档案 tCK 为零时报错，仍保留其它有效部分。
这项 EXPO 功能基于固定公开格式资料，是内存功能扩展，不宣称完成原版 EXPO 函数模拟。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38056197597) | 4/4：Ubuntu 22.04/24.04、Windows 2022、macOS 14；各 12 CTest、116 场景组 |
| ASan / UBSan | Ubuntu 24.04 的 SPD 解码、UMC 解析/比较均通过 |
| [独立 CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38056197668) | 12/12；十目标各 14 CTest、15 组命令场景、132 个打开前拒绝、278 份 JSON 报告 |
| [Linux GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38056197685) | 23/23；十目标与 EL8 基线各 60 Qt 测试；11 套内核 VM |
| 既有原指令 / 分析回归 | 74 项云端分析测试通过；沿用既有原版调参模拟证据，本轮没有新增原 EXPO 指令执行 |
| 原目录本机检查 | 48 项：36 通过、12 缺工具链/环境跳过；文档链接、确定性源码归档和回读通过 |

SPD 核心现在共 11 组场景，其中 5 组新增 EXPO 覆盖：数值与单位、0..1024 每个捕获长度、
128 字节区域逐位损坏、全部 256 种配置字节、未知版本及数值边界。
魔数损坏表示未检测到 EXPO，不强行解释为损坏扩展。
另检查 XMP 缺失、XMP 头 CRC 错误、共享区域启用冲突、DDR4 跳过和基础 CRC 错误。

CLI 新增十份模拟离线结果，另将原共存样例替换为有效 EXPO 块；共十九份 SPD 报告。
验证错误码、空值、单位、逐节状态和全过程不创建硬件后端。
GUI 新增一项复合回归，检查两档数值、坏 CRC、XMP 独立损坏、第二档独立启用和截断后清除旧数据。
截图等待布局完成后再滚动，并断言所选标题已进入视口顶部。

CLI 包在无显示服务、无 Qt、无构建工具的新环境中完成安装、普通用户运行、卸载和重装。
保存 340 张截图；EXPO/XMP 模拟窗口与内存页的人工检查范围见证据清单。
公开 EXPO 简单/增强样本的 CRC 和基础字段另由 Python 标准库本地核对；未把它们计为真机测试。

[XMP 用户档案判据实验](validation/xmp-user-presence-research.json)另记录四个合成 CRC 样例：
非零内容可以得到零 CRC，损坏的存储 CRC 也可能被上游 hasData 判据识别为有数据。
该实验只计算校验值，没有执行原版或上游函数；不证明时序合法或档案启用。
用户档案仍等待可核实的启用/存在性格式契约。

## 交付

`dist/gui-cli-ddr5-expo-e4cb81b/`：十目标各 GUI、CLI、可选 DKMS，共 30 个 Linux x86_64 包，
另附对应源码、源码 SHA256 侧车与 SHA256SUMS，共 33 个文件。
版本仍为研究版 2.0.1，以提交目录和校验值区分构建。

- GUI 包：10,479,154–14,261,184 字节；CLI 包：205,864–254,514 字节，不含可选驱动或系统运行库。
- 对应源码归档：3,997,166 字节。
- 18 个变更文件在原目录、云端工作树和源码归档间全部一致（规范化换行）。
- 其它 NVL 研究、打包改动和历史 UMC 文档差异保留；驱动/HAL/ABI 未改动。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,799,208 | 205,864 | 19,072 |
| el9 | 10,479,154 | 209,363 | 19,060 |
| el10 | 10,480,090 | 220,840 | 19,164 |
| ubuntu20.04 | 14,261,158 | 248,282 | 10,672 |
| ubuntu22.04 | 14,261,182 | 238,130 | 10,674 |
| ubuntu24.04 | 14,261,170 | 247,366 | 10,668 |
| ubuntu26.04 | 14,261,184 | 254,514 | 10,670 |
| debian11 | 14,261,184 | 233,714 | 10,666 |
| debian12 | 14,261,178 | 233,102 | 10,670 |
| debian13 | 14,261,178 | 242,934 | 10,670 |

EXPO 增强时序、1.1 版、用户档案和 DDR4 XMP 仍未完成；内存训练、SPD/PMIC/VRM 写入尚未恢复。
AMD PStates 写入、曲线/PBO 高层设置及其它 Intel/主板缺口见[还原状态](recovery-status.md)。
目标真机调参/稳定性、Secure Boot 实机登记和跨版本升级未验收。
跨平台核心测试不等于 Windows/macOS 硬件后端完成。
