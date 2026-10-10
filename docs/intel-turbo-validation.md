# Intel 睿频分组：GUI / CLI 同步验证

生产提交 `e43bad747785a35142a6f9127e53bcdaa1d5cee5`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/intel-turbo-ci-e43bad7.json)记录三个工作流、逐系统测试、安装包与源码 SHA256。
[实现说明](intel-turbo-recovery.md)记录原程序研究、公开来源、输入规则和型号边界。

本轮恢复 P/E 两类睿频分组：读取八组倍率及配对的活动核心数量阈值；
修改选中组的倍率，保留数量阈值和其余倍率；写前检查旧值、能力与锁，写后完整回读。
GUI 与 CLI 共用一个核心，当前限定 GenuineIntel family 6/model B7，E 核表另需混合架构能力。
这些是封装分组限制，逻辑 CPU 是访问位置；分组编号不是物理核心编号。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38028784706) | 4/4：Ubuntu 22.04/24.04、Windows 2022、macOS 14；各 8 CTest、81 场景组 |
| [Linux CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38028751967) | 12/12；十发行版各 10 CTest、11 组命令场景、93 个打开设备前拒绝、180 份 JSON |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38028751963) | 23/23；十发行版和 EL8 基线各 47 Qt 测试；11 套目标内核 VM 启动 |
| 原指令与分析回归 | 云端 73 项全部通过，其中新增 2 项重新执行原四个 writer 的 520 个场景并校验栈参数与失败返回 |
| 原目录本地检查 | 48 项，36 通过、12 因缺少 C++/Qt/Linux 工具链跳过；源码归档逐文件字节回读一致 |

新增核心的 9 个场景组覆盖两类表、全部八组、1..85 输入范围、倍率顺序、无效阈值、
型号/能力/锁、相同值跳过写入、旧值与阈值变化、每个读取/CPUID 故障位置、写失败、
完整读回的 64 位逐位损坏、取消和超时。
CLI 增加 19 份睿频成功/失败报告和 14 个参数拒绝场景，并检验原始 64 位值与逐组解码。
GUI 增加 4 项回归，覆盖 P/E 切换、复制、确认取消、写入及相同值、失败后清空旧编辑状态。

CLI 在无显示服务、无 Qt、无构建工具的新环境完成安装、运行、普通用户诊断和卸载重装。
归档 270 张 GUI 截图，检查了 Ubuntu 24.04 与 Rocky 8 的睿频模拟窗口，
以及 Ubuntu 24.04 的本机编译版和发行版睿频页。
八组配对值、原始值、输入区和读回状态可见；这些模拟值不是真机测量。

## 交付和源码同步

`dist/gui-cli-turbo-e43bad7/` 包含 30 个 DEB/RPM：十个目标各有 GUI、CLI 和可选 DKMS 包。
另有对应源码、源码侧车 SHA256 和总校验表 `SHA256SUMS`，共 33 个文件。
研究版仍标记 2.0.1，按提交目录和校验值区分构建。
交付源码固定在上述生产提交；后续验证文档不改变本次程序与安装包。

- GUI 安装包：10,356,271–14,061,248 字节。
- CLI 安装包：158,560–191,118 字节，不含可选 DKMS 和系统运行库。
- 源码归档：3,826,331 字节。
- 本轮 24 个功能、测试和研究文件与云端源码归档逐字节一致（仅规范化检出换行）。
- 原目录已有 NVL 研究、打包加固和历史文档保留；驱动、HAL 和 ABI 未变。

| 目标（x86_64） | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,658,168 | 158,560 | 19,072 |
| el9 | 10,356,271 | 162,554 | 19,059 |
| el10 | 10,357,576 | 167,208 | 19,163 |
| ubuntu20.04 | 14,061,214 | 187,246 | 10,670 |
| ubuntu22.04 | 14,061,226 | 182,528 | 10,668 |
| ubuntu24.04 | 14,061,220 | 181,918 | 10,664 |
| ubuntu26.04 | 14,061,210 | 191,118 | 10,668 |
| debian11 | 14,061,214 | 178,574 | 10,668 |
| debian12 | 14,061,194 | 173,400 | 10,668 |
| debian13 | 14,061,248 | 180,290 | 10,668 |

GUI 入口为 Intel Controls → Turbo ratio groups；CLI 提供 `intel-turbo-read` 和
`intel-turbo-set`，显式选择逻辑 CPU、P/E 类型、分组和倍率。
设置保留活动核心阈值。`verified:true` 表示配置完整回读一致，不代表实际频率或稳定性。
命令和权限用法见[CLI 手册](headless-cli.md)，两个入口范围见[功能对照](frontend-progress.md)。

## 尚未完成

睿频数量阈值编辑、逐物理核心 override、TVB/BCLK、VF 写入和 W790/W890 表仍未还原。
AMD PStates 写入、曲线设置与物理目标映射、完整 PBO 高层参数，
以及 Intel 训练时序、板级 PMIC/VRM/EC/时钟写入也仍有缺口，详见[还原状态](recovery-status.md)。

产物只针对 Linux x86_64；共同核心在 Windows/macOS 通过测试不代表这两个系统的硬件适配完成。
四台目标真机、调参稳定性、Secure Boot 实机部署和跨版本升级仍未验收。
已有事务锁只协调当前进程，不能排除其它程序或内核驱动并发修改寄存器。
