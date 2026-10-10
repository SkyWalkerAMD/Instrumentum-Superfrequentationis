# Intel Ring / LLC：GUI 与 CLI 同步验证

生产提交 `94b0655e2a093353aee8fe8d3cc0723a9807f7ca`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/intel-uncore-ci-94b0655.json)固定工作流、日志、安装包、源码及校验值。
[实现说明](intel-uncore-recovery.md)记录型号、原指令研究、寄存器范围和命令。

GUI 与 CLI 共用最小/最大倍率范围控制，限定 Intel family 6/model B7、8F。
一次写入同时提交两个边界，保留其它位，重新检查旧值并完整读回。
相同值跳过写入，失败不重试、不自动回滚；没有原版的 OC mailbox 附带修改。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38044299378) | 4/4：Ubuntu 22.04/24.04、Windows 2022、macOS 14；各 11 CTest、105 场景组 |
| 既有 UMC 回归 | Ubuntu 24.04 的 ASan/UBSan 解析与比较检查通过；这项不是新增 Ring/LLC 测试 |
| [独立 CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38044299343) | 12/12；十发行版各 13 CTest、14 组命令场景、132 个打开前拒绝、259 份模拟 JSON 报告 |
| [Linux GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38044299384) | 23/23；十发行版与 EL8 基线各 57 Qt 测试；11 套内核 VM 启动 |
| 原指令及分析 | 云端 74 项通过；新增 48 组原 cache-ratio 指令实验及有界 busy 循环 |
| 原目录本地检查 | 48 项，36 通过、12 因缺工具链/环境跳过；文档链接及源码归档回读通过 |

新增七组核心测试覆盖全部非零有序倍率对、64 位保留值、B7/8F、错误平台与虚拟机零 MSR 访问、
旧值逐位变化、每处 CPUID/读取故障、写失败、完整读回逐位损坏、固件忽略、取消和截止时间。
GUI 新增三项测试覆盖成对输入、取消/应用/无修改、CPU 变化、失败清空及读回状态；
CLI 增加 13 个打开设备前拒绝用例、16 份范围控制报告，并检查 JSON 空值和更新语义。
首轮较新编译器对测试循环的复制报告严格警告，已改用引用；本报告仅采用修正后同一提交的结果。

CLI 包在无显示服务、无 Qt、无构建工具的新环境完成安装、运行、普通用户诊断与卸载重装。
保存 320 张桌面截图；检查 Ubuntu 24.04 / Rocky 8 的新功能模拟窗口，以及 Ubuntu 24.04
本机编译版与发行版 Ring/LLC 页面。合成数据不是真机测量。

## 交付

`dist/gui-cli-intel-uncore-94b0655/`：十个 Linux x86_64 目标各有 GUI、CLI、可选 DKMS，共 30 个包。
加上对应源码、源码 SHA256 侧车与 SHA256SUMS，共 33 个文件。版本号仍为研究版 2.0.1，
以提交目录与哈希区分构建；后续验证文档不改变已验证程序。

- GUI 包：10,436,652–14,181,888 字节。
- CLI 包：191,648–236,534 字节，不含可选 DKMS 与系统运行库。
- 源码归档：3,924,451 字节。
- 本轮 25 个代码、功能文档及证据文件在云端归档、研究工作区、原目录之间逐一核对，规范化换行后一致。
- 原目录的 NVL 研究、打包加固和既有 UMC 文档链接保留；驱动、HAL、ABI 未改。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,746,260 | 191,648 | 19,072 |
| el9 | 10,436,652 | 192,842 | 19,062 |
| el10 | 10,436,733 | 203,941 | 19,165 |
| ubuntu20.04 | 14,181,888 | 228,128 | 10,668 |
| ubuntu22.04 | 14,181,728 | 220,442 | 10,664 |
| ubuntu24.04 | 14,181,730 | 227,234 | 10,668 |
| ubuntu26.04 | 14,181,716 | 236,534 | 10,670 |
| debian11 | 14,181,728 | 217,914 | 10,668 |
| debian12 | 14,181,728 | 216,272 | 10,670 |
| debian13 | 14,181,862 | 222,980 | 10,668 |

完整命令见 [CLI 手册](headless-cli.md)，两端功能范围见[功能对照](frontend-progress.md)。

## 验证边界

配置读回一致不代表实际倍率、频率、功耗或稳定性验收，OS 和其它进程仍可能修改同一寄存器。
此功能没有恢复其它型号的 uncore/fabric、多域 UFS、BCLK 或缓存电压协议。
AMD PStates 写入、曲线/完整 PBO 设置、Intel 其它型号和板级控制仍有缺口，详见[还原状态](recovery-status.md)。
核心跨平台测试不代表 Windows/macOS 硬件后端完成；四台目标真机、Secure Boot 实机部署与跨版本升级仍未验收。
