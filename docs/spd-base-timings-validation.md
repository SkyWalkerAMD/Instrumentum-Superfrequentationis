# DDR4 / DDR5 基础时序：GUI 与 CLI 同步验证

生产提交 `88530a9cc71d902772ad3ad90cbac4661fc202ba`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/spd-base-timings-ci-88530a9.json)绑定同一提交的云端运行、日志、安装包、源码和 SHA256。
[字段与使用说明](spd-base-timings.md)和[固定来源](validation/spd-base-timings-sources.json)记录偏移、单位和修订版条件。

GUI 与 CLI 同步补充 DDR4 tWR/tWTR_S/tWTR_L，以及 DDR5 基础区八对次级时序。
SPD 1.2 起另读取 tCCD_M/tCCD_M_WR/tCCD_M_WTR 三对，旧版本不会解释这些保留位置。
DDR4 使用 12 位计数和 125 ps 时间基准；DDR5 分别保留时间要求与独立周期下限。
新增字段保留零值，不推断 Auto、实际训练周期或可应用的配置。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38123233835) | 4/4；各 12 CTest、137 场景组，含 32 组 SPD 场景 |
| ASan / UBSan | Ubuntu 24.04 的 SPD 和 UMC 测试通过 |
| [独立 CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38123233886) | 12/12；十目标各 14 CTest、15 组命令场景、313 份 JSON 报告 |
| [Linux GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38123233832) | 23/23；十目标及 EL8 基线各 64 Qt 测试；11 套内核 VM |
| 既有原指令 / 分析回归 | 74 项通过；不代表执行了原版 SPD 时序函数 |
| 原目录本机检查 | 48 项：36 通过、12 缺环境/工具链跳过；链接、确定性归档和完整回读通过 |

新增六组 SPD 核心场景：DDR4 5 个字段字节、DDR5 33 个字段字节各自的全部 256 编码，
DDR4 共享高位/保留高位与无 FTB、DDR5 字段排序及其它档案隔离、时间与周期要求独立性、
零和最大编码、全部 256 修订版值和时间基准值、所有捕获长度及校验错误隔离。
公开 AQD 测试数据中的 5 / 24 个时序字节另放入合成模块，核对已知单位和结果；
没有把整份上游测试数据宣称为目标机器采集。

CLI 新增 12 份离线报告，共 54 份 SPD 报告，确认解码全过程不创建硬件后端。
GUI 新增一项测试，检查两种内存的数值、旧版本移除新字段、未知主版本、零值、CRC 失败、
不支持的 DDR4 时间基准和短文件清除旧行。保存 390 张截图，人工查看范围见证据清单。
十目标 CLI 在无显示、无 Qt、无构建工具的镜像中完成安装、普通用户运行、卸载和重装。

仅扩展基础 `fields`，值仍为十进制字符串、单位写在名称中，JSON 版本保持 1。
XMP / EXPO 档案、CAS 和命令率结构保持兼容；新增字段没有配置写入副作用。
DDR5 236..511 字节捕获仍可显示已捕获值，但基础 CRC 明确未验证。

## 交付

`dist/gui-cli-spd-base-timings-88530a9/` 包含十目标各 GUI、CLI、可选 DKMS，共 30 个包，
另附源码、源码 SHA256 侧车和 SHA256SUMS，共 33 个文件。
研究版号仍为 2.0.1，以提交目录和校验值区分。16 个本轮变更文件在原目录、云端工作树和
源码归档之间一致（规范化换行）；原目录的其它研究和打包改动保留。驱动/HAL/ABI 未改。
安装包与源码固定于生产提交；本报告和索引是随后单独提交的交付记录。
源码归档 4,119,658 字节，SHA256 `cebce2e0d3e5f76398ba1ad13676e06a204385f889b687e3eae84475ed979bfc`。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,876,652 | 219,884 | 19,072 |
| el9 | 10,550,011 | 221,212 | 19,060 |
| el10 | 10,551,476 | 233,683 | 19,164 |
| ubuntu20.04 | 14,373,818 | 260,644 | 10,668 |
| ubuntu22.04 | 14,373,872 | 252,782 | 10,668 |
| ubuntu24.04 | 14,373,870 | 262,524 | 10,668 |
| ubuntu26.04 | 14,373,808 | 269,958 | 10,668 |
| debian11 | 14,373,836 | 247,708 | 10,668 |
| debian12 | 14,373,860 | 249,774 | 10,670 |
| debian13 | 14,373,866 | 256,860 | 10,666 |

3DS 不同逻辑 rank 刷新要求、EXPO 增强时序启用、XMP 用户档案、实际训练值、SPD/PMIC/VRM 写入仍未完成。
AMD/Intel 其它缺口见[还原状态](recovery-status.md)。目标真机调参、稳定性、Secure Boot 登记和
跨版本升级未验收；核心在 Windows/macOS 通过不表示相应硬件后端完成。
