# Intel 控制与寄存器入口：GUI / CLI 同步验证

生产提交 `c0f97cc549583c1a6b85bad5f2002c3cd4cbc9de`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/controls-register-ci-c0f97cc.json)记录三个工作流、逐系统测试、安装包与源码 SHA256。
[实现说明](control-register-recovery.md)说明活动窗口编码、配置读回、原始寄存器访问与 UMC 导入。

本轮完成四项：GUI/CLI 共用 HWP 活动窗口；全部 13 项 RAPL/HWP 设置的完整配置读回；
CLI 的 MSR/MMIO/PCI 原始读写；CLI UMC 查询报告在 GUI 中离线导入、重算 212 字段与另存。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38023681945) | 4/4：Ubuntu 22.04/24.04、Windows 2022、macOS 14；各 7 CTest、72 场景组 |
| [Linux CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38023680671) | 12/12；十发行版各 9 CTest、10 组命令场景、79 个打开设备前拒绝、147 份 JSON |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38023680720) | 23/23；十发行版和 EL8 基线各 43 Qt 测试；11 套目标内核 VM 启动 |
| 原指令与分析回归 | 云端 71 项全部通过；既有原目标电压 82 场景继续通过 |
| 原目录本地检查 | 48 项，36 通过、12 因缺少 C++/Qt/Linux 工具链跳过；源码归档逐文件字节回读一致 |

新增场景覆盖全部 13 项控制、1024 种窗口编码、取整、相同配置跳过写入、锁和过期值、
寄存器其它位变化、身份/能力/单位变化、每个读取与写入失败位置、取消与超时。
GUI 同时验证确认取消、成功后的输入清空和新快照、失败时旧编辑状态清空。
原始寄存器测试验证 64 位边界、各访问宽度、完整参数门控和只提交一次。
UMC 两个入口共同校验一份模拟报告；GUI 覆盖错误报告、重复/缺少/未知寄存器等 14 类无效输入。

CLI 在无显示服务、无 Qt、无构建工具的新环境完成安装、运行、普通用户诊断和卸载重装。
归档 240 张 GUI 截图，人工检查 Ubuntu 24.04 与 Rocky 8 的 Intel Controls 模拟窗口，
确认活动窗口单位、读回状态与清空后的输入可见。截图和寄存器响应均不是真机测量。

## 交付和源码同步

`dist/gui-cli-controls-c0f97cc/` 包含 30 个 DEB/RPM：十个目标各有 GUI、CLI 和可选 DKMS 包。
另有对应源码、源码侧车 SHA256 和总校验表 `SHA256SUMS`，共 33 个文件。
研究版仍标记 2.0.1，按提交目录和校验值区分构建。
交付源码固定在上述生产提交；后续补充的验证文档不改变本次程序与安装包。

- GUI 安装包：10,323,163–14,003,714 字节。
- CLI 安装包：152,632–183,838 字节，不含可选 DKMS 和系统运行库。
- 源码归档：3,787,567 字节。
- 本轮 19 个功能、测试和研究文件与云端源码归档逐字节一致（仅规范化检出换行）。
- 原目录已有 NVL 研究、打包加固和历史文档保留；驱动、HAL 和 ABI 未变。

| 目标（x86_64） | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,619,912 | 152,632 | 19,072 |
| el9 | 10,323,163 | 156,305 | 19,058 |
| el10 | 10,324,295 | 160,558 | 19,166 |
| ubuntu20.04 | 14,003,696 | 179,640 | 10,670 |
| ubuntu22.04 | 14,003,698 | 175,156 | 10,664 |
| ubuntu24.04 | 14,003,512 | 174,862 | 10,670 |
| ubuntu26.04 | 14,003,514 | 183,838 | 10,672 |
| debian11 | 14,003,516 | 171,234 | 10,670 |
| debian12 | 14,003,714 | 166,828 | 10,672 |
| debian13 | 14,003,516 | 172,942 | 10,666 |

HWP 窗口位于 Intel Controls → Power / performance；CLI 字段为 `hwp-window-us`，单位微秒，
0 请求硬件自动选择。实际支持由 CPU 能力、HWP 启用状态与当前配置决定。
全部高级控制设置返回配置读回结果，读回一致不代表实际功耗、频率或稳定性。
CLI 原始寄存器写入不自动预读或回读，结果明确 `verified:false`。
命令和权限用法见[CLI 手册](headless-cli.md)，两种入口范围见[功能对照](frontend-progress.md)。

## 尚未完成

原版全部功能尚未还原：AMD PStates 写入、曲线设置和物理核心映射、完整 PBO 高层参数；
Intel VF 写入、逐核及 W790/W890 电压/fabric；Intel 训练时序和板级 PMIC/VRM/EC/时钟写入。
[Shimada 协议交叉核对](amd-shimada-crosscheck.md)明确了不同实现的命令与访问通道差异，
尚不足以认证新增 AMD 写入。详细障碍见[还原状态](recovery-status.md)。

产物只针对 Linux x86_64；共同核心在 Windows/macOS 通过测试不代表这两个系统的硬件适配完成。
四台目标真机、调参稳定性、Secure Boot 实机部署和跨版本升级仍未验收。
已有事务锁只协调当前进程。GUI 可以导入 CLI UMC 报告，CLI 尚无读取 GUI 快照文件的独立命令。
