# SPD CAS 与 XMP 命令率：GUI 与 CLI 同步验证

生产提交 `6aca371685aad014dbea3d62ee2395b7f08018c7`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/spd-cas-ci-6aca371.json)绑定同一提交的云端运行、日志、安装包、源码和 SHA256。
[字段与使用说明](spd-cas-recovery.md)和[固定来源](validation/spd-cas-sources.json)记录格式依据及限制。

GUI 与 CLI 同步新增 DDR4/DDR5 基础 SPD 的 CAS 支持列表，以及 DDR5 XMP 3.0
三个厂家档案的 CAS 列表和命令率。CAS 来自原始位图，不用 tAA/tCK 推算；列表和
命令率描述档案要求，不表示当前训练结果，也不构成配置应用。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38121792862) | 4/4；各 12 CTest、131 场景组，含 26 组 SPD 场景 |
| ASan / UBSan | Ubuntu 24.04 的 SPD 和 UMC 测试通过 |
| [独立 CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38121792949) | 12/12；十目标各 14 CTest、15 组命令场景、301 份 JSON 报告 |
| [Linux GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38121792834) | 23/23；十目标及 EL8 基线各 63 Qt 测试；11 套内核 VM |
| 既有原指令 / 分析回归 | 74 项通过；不代表执行了原版 CAS/命令率解码函数 |
| 原目录本机检查 | 48 项：36 通过、12 缺环境/工具链跳过；链接、确定性归档和完整回读通过 |

新增五组 SPD 核心场景，覆盖 DDR4 两个范围的所有位、DDR5 全部 40 个 CAS 位、
三个 XMP 档案之间的隔离、位图五字节各自的全部 256 编码，以及命令率字节全部 256 编码。
时间字段变化不会改变位图列表；DDR4 保留位不会被误解释为 CAS。
短捕获、未知修订版、CRC 损坏、禁用、零 tCK 和 EXPO 重叠均有检查。

CLI 新增八份离线报告，共 42 份 SPD 报告，验证此命令全过程不创建硬件后端。
未捕获、未知含义和合法零位图区分为 null、保留原值且不解码、空列表。
命令率未知代码不会伪装成已知数值或零，也不会隐藏其它有效档案字段。
GUI 新增一项测试，检查三组 CAS/命令率、40 项长列表的分行显示、未知代码和重新加载清除旧值。
保存 370 张截图，人工查看范围见证据清单。十目标 CLI 在无显示、无 Qt、无构建工具的镜像中
完成安装、普通用户运行、卸载和重装。

初次提交 `38956d5` 的 Windows 编译因测试位图填充的隐式整数到字节转换失败；
最终提交显式使用字节类型。上表全部运行及交付包均来自修复后的同一提交，未复用旧提交的通过结果。

公开 XMP 独立档案的 CAS 字节 `7aed070000` 与命令率代码 2 在本机按固定哈希和 CRC 检查；
合成模块采用独立标记验证地址与排序。公开样本和合成测试不构成目标主板 EEPROM 采集、
内存训练或稳定性验收。DDR4 XMP 的公开定义存在范围差异，未把基础区布局直接套用到扩展档案。

## 交付

`dist/gui-cli-spd-cas-6aca371/` 包含十目标各 GUI、CLI、可选 DKMS，共 30 个包，
另附源码、源码 SHA256 侧车和 SHA256SUMS，共 33 个文件。
研究版号仍为 2.0.1，以提交目录和校验值区分。18 个本轮变更文件在原目录、云端工作树和
源码归档之间一致（规范化换行）；原目录的其它研究和打包改动保留。驱动/HAL/ABI 未改。
安装包与源码固定于生产提交；本报告和索引是随后单独提交的交付记录。
源码归档 4,088,634 字节，SHA256 `b7a8019af98da7e6c1d00b73642d6d57347d6e5ad3ccc2a5bc1ddcb3deab16cf`。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,858,184 | 217,492 | 19,068 |
| el9 | 10,532,780 | 218,273 | 19,052 |
| el10 | 10,534,816 | 231,343 | 19,154 |
| ubuntu20.04 | 14,344,750 | 258,540 | 10,670 |
| ubuntu22.04 | 14,344,872 | 248,550 | 10,668 |
| ubuntu24.04 | 14,344,752 | 258,394 | 10,668 |
| ubuntu26.04 | 14,344,880 | 267,432 | 10,668 |
| debian11 | 14,344,882 | 244,006 | 10,666 |
| debian12 | 14,344,880 | 245,738 | 10,670 |
| debian13 | 14,344,904 | 254,400 | 10,666 |

DDR4 XMP CAS/每通道 DIMM 配置、XMP 用户档案、EXPO 增强时序/1.1、SPD/PMIC/VRM 写入仍未完成。
AMD/Intel 其它缺口见[还原状态](recovery-status.md)。目标真机调参、稳定性、Secure Boot 登记和
跨版本升级未验收；核心在 Windows/macOS 通过不表示相应硬件后端完成。
