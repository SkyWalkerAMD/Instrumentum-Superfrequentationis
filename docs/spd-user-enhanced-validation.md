# XMP 用户槽与 EXPO 增强存储字段：验证交付

生产提交 `75c5c8b69b13c9751513c546a5a65fa2ace0b923`，研究分支 `refactor/platform-recovery`。
[证据清单](validation/spd-user-enhanced-ci-75c5c8b.json)绑定任务、日志、截图、包与源码哈希。
[实现说明](spd-user-enhanced.md)区分存储字节、校验、启用状态和当前实际设置。

GUI / CLI 同步新增两个 XMP 3.0 用户槽：独立捕获状态、CRC、电压、基础/次级时序、CAS 与命令率；
EXPO 1.0 两组各新增八个增强时序存储字，单位 ps。用户槽和增强功能启用语义均仍未知。
CRC 恰好为零的有效非零数据可以解码；EXPO 区域冲突、截断、错误 CRC 与未知版本独立显示。

| 验证层 | 结果 |
|---|---|
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38130003195) | 4/4；各 12 CTest、154 场景组，含 49 组 SPD 场景 |
| ASan / UBSan | SPD 和 UMC 内存及未定义行为检查通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38130005949) | 12/12；十目标各 14 CTest、15 组命令场景、342 份 JSON，含 83 份 SPD 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38130002165) | 23/23；十目标及 EL8 基线各 68 Qt 测试，11 套内核 VM |
| 既有原指令/分析回归 | 74 项通过，不表示执行了原版 SPD 函数 |
| 原目录本机 | 48 项：36 通过、12 因本机缺少工具链/环境跳过；归档完整回读和确定性检查通过 |

最初提交 `755122f` 的旧测试认为 EXPO 标志损坏后 XMP 部分不会报错。
新增用户槽检查会独立发现这段存储字节的 CRC 错误；修正断言，要求该错误出现且有效厂家档案保留。
保留首次失败日志；生产提交 `75c5c8b` 未修改解码器，完整矩阵重新通过。
截图检查另发现较窄窗口截断增强启用状态；最终提交缩短状态文字，对应 GUI 断言已同步，
重新生成同一提交的 GUI/CLI 包与验证记录。

CLI 十目标均完成无显示、无 Qt、无构建工具环境的安装、普通用户启动、卸载和重装。
保存 430 张截图，实际查看范围见证据清单；没有把合成 SPD 当作真机数据。
打包时核对本轮同步的 19 个文件：18 个经换行及既有 UMC 链接规范化后与生产源码一致；
`platform-controls.md` 的 SPD 行一致，其独立 UMC 内容保留。随后仅更新验证文档与旧状态表述，
12 个运行代码、测试和构建脚本仍与生产源码一致。未改驱动/HAL/ABI。

## 交付

`dist/gui-cli-spd-user-enhanced-75c5c8b/`：10 GUI、10 CLI、10 可选 DKMS 安装包，
以及源码、源码 SHA256 侧车和 SHA256SUMS，共 33 文件。
版号仍为 2.0.1；用提交目录和校验值区分本轮产物。安装包和源码固定在生产提交，
本验证文档与 [AMD PM 表后续研究](amd-pm-table-followup.md)记录在独立的文档提交。

源码 4,184,490 字节；SHA256 `4559cfcc706394b1a915add059dfedea74f4140ba17367983d44a3b2e31c0110`。

| 目标 | GUI 包字节 | CLI 包字节 | DKMS 包字节 |
|---|---:|---:|---:|
| el8 | 10,922,032 | 229,468 | 19,072 |
| el9 | 10,594,622 | 230,923 | 19,059 |
| el10 | 10,595,020 | 241,029 | 19,164 |
| ubuntu20.04 | 14,435,022 | 272,156 | 10,664 |
| ubuntu22.04 | 14,435,072 | 263,340 | 10,672 |
| ubuntu24.04 | 14,435,044 | 271,840 | 10,670 |
| ubuntu26.04 | 14,435,036 | 279,000 | 10,666 |
| debian11 | 14,435,078 | 259,870 | 10,662 |
| debian12 | 14,435,032 | 260,724 | 10,668 |
| debian13 | 14,435,068 | 265,222 | 10,670 |

原版仍未全部还原。XMP 用户槽/EXPO 增强启用契约、XMP 3.0 推荐 DIMM 数、EXPO 1.1/用户档案、
非对称容量、训练值、SPD/板级写入以及既有 AMD/Intel 写入缺口仍保留。
四台目标真机调参、稳定性、Secure Boot 和跨版本升级未验收。
Windows/macOS 的核心测试不表示其完整硬件适配已完成。
