# AMD PStates 电压/电流整批验证

2026-10-18：生产提交 `d02f8fc7cb89fd074e23f2ff17fb6dc92f34c13a`。GUI / CLI 共用解码，新增五个明确型号的
VID 电压与预期最大单核电流描述量，区分 SVI2 / SVI3、关闭编码、保留位及错误状态。
[实现说明](amd-pstates-electrical.md)、[完整验证记录](validation/amd-pstates-electrical-ci-d02f8fc.json)。

| 检查 | 结果 |
|---|---|
| 依据 | 固定 coreboot 提交的 CPU 表、平台选择、MSR 布局及换算调用；24 个源码对象哈希核对 |
| [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39100294995) | 4/4；各 24 CTest / 345 场景组，其中 AMD PStates 18 组 |
| ASan / UBSan | 原有五个快照解析目标通过 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39100294974) | 13/13；11 目标各 26 CTest / 46 CLI 场景组 / 1055 份 JSON，新增 27 份 P-state 电气字段报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39100294948) | 26/26；各目标及 EL7/EL8 基线 169 项 Qt 回归；104 分析测试、33 移植测试 |
| 匹配内核 QEMU | 12 次启动，含 EL7 3.10；装卸、能力、非法 CPU、非特权拒绝通过 |
| 本机用户源码移植测试 | 49 项：37 通过，12 因缺 Linux/C++ 工具链跳过；云端对应检查完整执行 |
| 真机 / Secure Boot | 未执行 |

43/43 云端任务对应同一生产提交。CLI 在无显示、无构建工具、无硬件设备的新环境
完成安装、运行、卸载及重装，依赖中无 Qt/X/Wayland。新增回归覆盖 VID 第九位和小数步进、
关闭/保留编码、全部 Idd 编码、精确型号选择、虚拟化、读取错误、视图切换及旧数据清空。

保留 979 张自动截图；人工检查 Ubuntu 24.04 与 Oracle Linux 7.9 的 SVI2、SVI3、部分失败
页面共 6 张。其他截图未逐张人工检查。截图中是合成值，不是真机电压/电流。

`dist/gui-cli-amd-pstates-electrical-d02f8fc/` 包含 33 个 GUI/CLI/DKMS 包，覆盖
Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、Oracle Linux 7.9、Rocky Linux 8/9/10。
EL7 验证范围为 Oracle Linux 7.9 x86_64，其他 EL7 配置仍需验收。
根目录源码包对应精确云端提交；`updated-source/` 源码保留本地扩展和最新验证文档。
270 个生产/构建源码路径逐一核对，保留差异列在 MANIFEST；SHA256SUMS 覆盖全部交付文件。
包版本仍为 2.0.1，同版本更新使用发行版的本地重装方式。

另对已固定原 ELF 的 `cal_vid_from_raw` 做静态核对：读取原指令引用的 1550、6.25、245、5
四个 double 常量及平台标志地址，确认其结果转整数、未单独处理关闭编码。
[记录](validation/amd-pstates-electrical-original.json)包含地址和哈希；未执行原函数或硬件，
该项在生产提交之后完成，不计入上述云端测试数。平台标志本身不认证所有 CPU 的电气模型。

本批支持 17h/11h、18h、20h、A0h 和 19h/50h，全部为描述值读取。
其它型号（含 1Ah）电气换算、PStates 写入、AMD 高层调参、ICCmax/VR、Vmax/增强 TVB、
板级控制及真机验收仍未完成，见[剩余清单](recovery-status.md)。
