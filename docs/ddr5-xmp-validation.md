# DDR5 XMP：GUI 与 CLI 同步验证

生产提交 `4932f1bd02a91302f721cecf38768be191fb9878`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/ddr5-xmp-ci-4932f1b.json)固定同一提交的运行、日志、安装包、源码和校验值。
[实现说明](ddr5-xmp-recovery.md)记录原版入口、字节布局、来源及边界。

GUI 和 CLI 同步解码 XMP 3.0 的三个厂家档案：启用位、名称、四项电压及十项原始时序。
头和每个档案独立检查 CRC；错误不伪装为有效数据，也不遮住其它已通过校验的部分。
EXPO 区域仅识别，避免误读成第三个 XMP 档案。没有 EEPROM 写入或配置应用。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38048131383) | 4/4：Ubuntu 22.04/24.04、Windows 2022、macOS 14；各 12 CTest、111 场景组 |
| ASan / UBSan | Ubuntu 24.04 的新增 SPD 解码与既有 UMC 解析/比较测试通过 |
| [独立 CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38048131391) | 12/12；十发行版各 14 CTest、15 组命令场景、132 个打开前拒绝、268 份 JSON 报告 |
| [Linux GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38048131414) | 23/23；十发行版与 EL8 基线各 59 Qt 测试；11 套内核 VM |
| 原指令及分析回归 | 既有 74 项云端分析测试通过；另完成本地 8 种原启用分支局部执行，跳过档案体，未模拟整函数 |
| 原目录本地检查 | 48 项：36 通过、12 缺工具链/环境跳过；文档链接、确定性源码归档及回读通过 |

新增六组核心场景覆盖 0..1024 每个捕获长度、XMP 头和三个档案的逐位损坏、所有启用位组合、
未知版本、EXPO 共存/冲突、16 字节名称与整数/单位边界。头标志位被修改时按不存在处理，
不把不匹配的魔数任意判断为损坏 XMP。启用档案的无效值不发布，原始字节与校验结果仍保留。
GUI 新增两项回归覆盖数值、独立状态、错误清空、共存和部分文件；CLI 新增九份离线报告，
断言全过程不创建硬件后端，并检查类型、空值、错误和单位。

[局部分支实验](validation/legacy-ddr5-xmp-enable-dispatch-local.json)在固定原 ELF 上确认：
enable mask 为 2、4、6（未开启档案 1）时，原入口跳过其它档案；新解码器独立检查三个启用位。
实验只执行条件跳转和起始偏移选择，档案体被跳过；没有执行完整原函数、SMBus、Qt 或硬件访问。

首次 Windows 严格编译发现测试填充值的整数窄化警告，已显式使用字节类型；
本报告只使用修正后同一提交的全矩阵结果。

CLI 包在无显示服务、无 Qt、无构建工具的新环境安装、运行、普通用户诊断并卸载重装。
共保存 330 张截图，检查 Ubuntu 24.04 / Rocky 8 的 SPD 模拟档案窗口与 Ubuntu 24.04
本机编译版/发行版内存页。模拟显示不是实机 EEPROM 或运行参数认证。
公开 TEAMGROUP 样本的五个 CRC 区域另由 Python 标准库本地核对，见来源清单；
它没有被计作新解码器的实机测试。

## 交付

`dist/gui-cli-ddr5-xmp-4932f1b/`：十目标各有 GUI、CLI、可选 DKMS，共 30 个 Linux x86_64 包；
另附对应源码、源码 SHA256 侧车与 SHA256SUMS，共 33 个文件。
版本号仍为研究版 2.0.1，以提交目录和 SHA256 区分构建。

- GUI 包：10,465,231–14,224,798 字节。
- CLI 包：199,260–247,780 字节，不含可选 DKMS 或系统运行库。
- 源码归档：3,970,049 字节。
- 22 个变更文件均与云端源码归档核对；其中 21 个在原目录完全一致（规范化换行）。
  `platform-controls.md` 只合入 SPD 行，其它既有 UMC 研究文本和链接保留并单独记录哈希。
- 原目录其它 NVL 研究、打包改动及 UMC 文档差异保留；驱动/HAL/ABI 未变。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,779,424 | 199,260 | 19,068 |
| el9 | 10,465,231 | 203,154 | 19,058 |
| el10 | 10,465,798 | 214,310 | 19,161 |
| ubuntu20.04 | 14,224,616 | 239,258 | 10,668 |
| ubuntu22.04 | 14,224,730 | 231,176 | 10,670 |
| ubuntu24.04 | 14,224,798 | 238,716 | 10,668 |
| ubuntu26.04 | 14,224,750 | 247,780 | 10,668 |
| debian11 | 14,224,760 | 225,684 | 10,668 |
| debian12 | 14,224,764 | 226,712 | 10,668 |
| debian13 | 14,224,612 | 234,966 | 10,668 |

XMP 用户档案、EXPO 参数和 DDR4 XMP 解码仍未实现；内存训练、SPD/PMIC/VRM 写入也未恢复。
AMD PStates 写入、曲线/PBO 高层设置和其它 Intel/主板功能缺口见[还原状态](recovery-status.md)。
四台目标真机、Secure Boot 实机登记和跨版本升级未验收；跨平台核心测试不代表 Windows/macOS 硬件后端完成。
