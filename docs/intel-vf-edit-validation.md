# Intel V/F 单点设置：GUI / CLI 同步验证

生产提交 `32c89ae389174a0c822e56ec401d65ccc88f4b03`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/intel-vf-edit-ci-32c89ae.json)固定三个工作流、测试日志、各系统安装包与源码校验值。
[实现及来源](intel-vf-write-recovery.md)记录点设置载荷与查询值的区别，以及型号和配置边界。

GUI 与 CLI 同时恢复 core/cache 的单点 offset 设置，限定 GenuineIntel family 6/model B7。
写入要求锁清除、该域 Adaptive 且全域目标/offset 为零；core 另要求 per-core override 关闭。
程序不自动复位全域设置、不发送切换 override 的 0x15，也不设置点倍率。
提交前核对旧值，提交后复核完整点值及准备上下文；同值不发设置命令，失败不重试或回滚。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38032688225) | 4/4：Ubuntu 22.04/24.04、Windows 2022、macOS 14；各 9 CTest、90 场景组 |
| [独立 CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38032688257) | 12/12；十发行版各 11 CTest、12 组命令场景、109 个设备打开前拒绝、220 份 JSON |
| [Linux GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38032688308) | 23/23；十发行版和 EL8 基线各 51 Qt 测试；11 套内核 VM 启动 |
| 原指令及分析 | 云端 73 项全部通过；包含重新执行原 VF 指令的 135 组场景及 override 副作用检查 |
| 原目录本地检查 | 48 项，36 通过、12 因缺少工具链/环境跳过；文档链接和源码归档回读通过 |

新增九组核心场景检验两个域的所有候选点、2048 种 offset 编码、配置权限、过期上下文、
每个传输故障位置、固件状态、busy 上限、32 位读回逐位变化、取消和超时。
CLI 增加 16 个参数拒绝及 24 份编辑查询/提交报告，检查完整原始值、错误空值和设置命令状态。
GUI 增加四项测试，覆盖目标切换、准备、输入、取消、设置、同值跳过、只读条件和失败后清空。
首轮云端发现测试代码的有符号/无符号比较不符合 MSVC 的严格检查，已修复并按本提交重新验证。

CLI 在无显示服务、无 Qt、无构建工具的新环境完成安装、运行、普通用户诊断和卸载重装。
归档 280 张 GUI 截图，检查 Ubuntu 24.04 / Rocky 8 的单点设置模拟窗口和 Ubuntu 24.04
本机编译版、发行版的 V/F 页面。目标、上下文、输入和读回状态清晰可见；模拟值不是真机测量。

## 交付

`dist/gui-cli-vf-edit-32c89ae/` 包含十个 Linux x86_64 目标各自的 GUI、CLI、可选 DKMS，共 30 个包。
加上对应源码、源码 SHA256 侧车和总校验表 `SHA256SUMS`，共 33 个文件。
研究版仍标记 2.0.1，以提交目录与校验值区分构建；后续验证文档不改变已验证程序。

- GUI 安装包：10,379,503–14,098,368 字节。
- CLI 安装包：163,568–196,614 字节，不含可选 DKMS 和系统运行库。
- 源码归档：3,854,328 字节。
- 本轮 19 个文件与生产源码核对：18 个规范化换行后完全一致；还原状态文档保留原目录既有 UMC 文档链接，其余内容一致。
- 原目录的 NVL 研究、打包加固等独立改动保留；驱动、HAL、ABI 未修改。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,683,272 | 163,568 | 19,068 |
| el9 | 10,381,280 | 167,044 | 19,059 |
| el10 | 10,379,503 | 172,323 | 19,161 |
| ubuntu20.04 | 14,098,368 | 194,402 | 10,670 |
| ubuntu22.04 | 14,098,314 | 188,740 | 10,666 |
| ubuntu24.04 | 14,098,312 | 188,434 | 10,666 |
| ubuntu26.04 | 14,098,352 | 196,614 | 10,666 |
| debian11 | 14,098,308 | 184,928 | 10,670 |
| debian12 | 14,098,362 | 179,404 | 10,664 |
| debian13 | 14,098,158 | 186,674 | 10,672 |

GUI：Intel Controls → V/F points → 选择单点 → Prepare selected point → 输入偏移 → Apply point offset。
CLI：`intel-vf-read ... --point P --for-edit` 查询编辑条件，`intel-vf-set ... --point P --value MV --apply` 提交。
完整用法见[CLI 手册](headless-cli.md)，两端功能范围见[功能对照](frontend-progress.md)。

## 验证边界

`verified:true` 仅表示选定点及准备上下文的配置读回相符；没有逐点测量其它 VF 点、
实际电压、频率或稳定性。事务锁仅保护本进程，不能排除其它程序和驱动并发访问。
共同核心在 Windows/macOS 测试通过不代表这两个系统的硬件适配完成。

其它 Intel 型号、逐物理核心模式、W790/W890 电压域，AMD PStates/曲线设置、完整 PBO 高层参数、
Intel 训练时序和板级专用写入仍有缺口，见[还原状态](recovery-status.md)。
四台目标真机、调参稳定性、Secure Boot 实机部署和跨版本升级仍未验收。
